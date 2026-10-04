# SPDX-License-Identifier: GPL-3.0-or-later
"""Reviewed-plan cleanup; report candidates or verify a previously completed audit."""
import argparse
import contextlib
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import sys
import uuid

from maintenance_paths import (LockedFile, build_lock, contained, digest,
                               new_json, no_reparse, read_json, require, snapshot, workspace)

AUDIT = '.local/cleanup-audit/2026-10-03'
INTERMEDIATE_ROOT = 'outputs/pre-relocation-build/'
INTERMEDIATE_EXTENSIONS = {'.obj', '.pch', '.tlog', '.lastbuildstate', '.exp'}
RETAINED = '.local/cleanup-retained'
PROTECTED = [
    'dist', 'Deployments', 'build (handled separately by Clean-ClipboardBuild.ps1)',
    'src', 'assets', 'localization', 'config', 'docs', 'tests', 'tools', 'external',
    'Original Project Files', 'legacy', 'CMakeUserPresets.json',
    '.local (private documents, rollback, audit and evidence)',
    'outputs except exact audited candidates: preserve saves, captures, reports, DLL/PDB sets and xEdit fixtures',
    'all configured inputs, including paths outside the workspace',
]


def utc():
    return datetime.now(timezone.utc).isoformat()


def policy(root, audit):
    contained(root, audit)
    require(audit.startswith('.local/cleanup-audit/'), 'Audit must stay in .local/cleanup-audit')
    runtime = read_json(contained(root, audit + '/runtime-database-duplicates.json'))
    intermediates = read_json(contained(root, audit + '/pre-relocation-build-review.json'))
    # The seven-byte negative fixtures are deliberately excluded.
    groups = [g for g in runtime if g['bytesPerFile'] > 1024 * 1024 and g['copies'] > 1]
    require(len(groups) == 1, 'Expected exactly one reviewed full runtime database hash group')
    group = groups[0]
    require(len(group['paths']) == group['copies'], 'Invalid runtime path count')
    allowed = {}
    for path in group['paths']:
        contained(root, path)
        require(path.startswith('outputs/') and path.endswith('/Data/F4SE/Plugins/f4rd-runtime.bin')
                and '/xedit/' not in path.lower(), f'Protected runtime path: {path}')
        require(path.casefold() not in allowed, 'Duplicate audited path')
        allowed[path.casefold()] = ('retain-runtime', path)
    for row in intermediates['narrowIntermediateCandidates']:
        path = row['path']
        contained(root, path)
        require(path.startswith(INTERMEDIATE_ROOT) and Path(path).suffix.lower() in INTERMEDIATE_EXTENSIONS,
                f'Protected intermediate path: {path}')
        require(path.casefold() not in allowed, 'Duplicate audited path')
        allowed[path.casefold()] = ('remove-intermediate', path)
    return allowed, group


def walk_files(root, relative):
    start = contained(root, relative)
    if not start.exists():
        return
    for directory, dirs, files in os.walk(start, followlinks=False):
        for name in dirs + files:
            no_reparse(Path(directory) / name)
        for name in sorted(files):
            yield (Path(directory) / name).relative_to(root).as_posix()


def guard_paths(root, audit):
    paths = {audit + '/' + name for name in (
        'runtime-database-duplicates.json', 'pre-relocation-build-review.json',
        'preserved-archives.json', 'preserved-records.json')}
    for name in ('preserved-archives.json', 'preserved-records.json'):
        for record in read_json(contained(root, audit + '/' + name)):
            current = snapshot(root, record['path'])
            require(current['sha256'] == record['sha256'].lower(),
                    f'Audit preservation baseline changed: {record["path"]}')
            paths.add(record['path'])
    # Current code/config and attestations bind a plan to its consumer review.
    for name in ('tools', 'config', 'src', 'tests', 'docs/development',
                 'build/metadata/native', 'build/papyrus/current'):
        paths.update(p for p in walk_files(root, name)
                     if '/__pycache__/' not in p and not p.endswith('.pyc'))
    paths.update(walk_files(root, 'dist'))
    paths.update(walk_files(root, 'Deployments'))
    for name in ('AGENTS.md', 'CMakeUserPresets.json', 'CMakePresets.json', 'vcpkg.json', 'Build.ps1',
                 '.local/build-paths.json'):
        if contained(root, name).exists():
            paths.add(name)
    return sorted(paths)


def configured_paths(root):
    """Conservative additional exclusions from active records, presets and environment."""
    values = []
    visited = set()

    def visit(value):
        if isinstance(value, dict):
            for item in value.values():
                visit(item)
        elif isinstance(value, list):
            for item in value:
                visit(item)
        elif isinstance(value, str):
            expanded = value.replace('${sourceDir}', str(root))
            if os.path.isabs(expanded):
                values.append(os.path.normcase(os.path.abspath(expanded)))
            elif expanded.replace('\\', '/').startswith(('outputs/', 'build/')):
                values.append(os.path.normcase(os.path.abspath(root / expanded)))

    def read_config(path):
        path = Path(os.path.abspath(path))
        no_reparse(path)
        if path in visited or not path.exists():
            return
        visited.add(path)
        value = read_json(path)
        visit(value)
        # CMake permits nested/local/external include files.
        if isinstance(value, dict):
            for include in value.get('include', []):
                include = include.replace('${sourceDir}', str(root))
                read_config(path.parent / include)
            compiler = value.get('compiler')
            if compiler:
                values.append(os.path.normcase(os.path.abspath(Path(compiler).parent.parent)))

    for name in ('CMakeUserPresets.json', 'CMakePresets.json',
                 'build/papyrus/current/papyrus-build.json', 'build/metadata/native/native-build.json'):
        read_config(root / name)
    local_paths = contained(root, '.local/build-paths.json')
    if local_paths.exists():
        # Persistent discovery configuration can contain workspace-relative inputs.
        for value in read_json(local_paths).values():
            if isinstance(value, str):
                values.append(os.path.normcase(os.path.abspath(root / value)))
    for name, value in os.environ.items():
        if name.upper().startswith('CLIPBOARD_') or name.upper() == 'VCPKG_ROOT':
            visit(value)
    return sorted(set(values))


def check_configured(root, path, configured):
    absolute = os.path.normcase(str(contained(root, path)))
    require(not any(absolute == value or absolute.startswith(value.rstrip('\\/') + os.sep)
                    for value in configured), f'Active configured input rejected: {path}')


def completed_audit(root, audit, allowed, group, missing):
    """Recognize consumed audits from retained evidence, never from absence alone."""
    require(len(missing) == len(allowed),
            f'Audit is stale or partly applied: {len(missing)} of {len(allowed)} candidates are absent. '
            'Review .local/cleanup-runs and create a fresh audit; no files were removed.')
    runs = contained(root, '.local/cleanup-runs')
    for run in sorted(runs.iterdir()) if runs.exists() else []:
        relative = run.relative_to(root).as_posix()
        saved_path = contained(root, relative + '/reviewed-plan.json')
        journal_path = contained(root, relative + '/journal.jsonl')
        if not saved_path.is_file() or not journal_path.is_file():
            continue
        plan = read_json(saved_path)
        if plan.get('audit') != audit or plan.get('workspace') != str(root):
            continue
        events = [json.loads(line) for line in journal_path.read_text(encoding='utf-8').splitlines() if line.strip()]
        if not events or events[-1].get('event') != 'complete':
            continue
        count = len(allowed)
        require(plan.get('schema') == 1 and len(plan['candidates']) == count and
                {item['path'].casefold(): (item['action'], item['path']) for item in plan['candidates']} == allowed and
                all(item['selected'] is True for item in plan['candidates']),
                'Completed plan does not cover this whole audit; create a fresh audit.')
        plan_sha = events[0].get('planSha256', '').lower()
        require(re.fullmatch(r'[0-9a-f]{64}', plan_sha), 'Invalid completed journal plan digest')
        # The retained plan is serialized anew by apply_plan. Bind its semantic
        # contents to the exact reviewed bytes whose digest was journaled.
        originals = contained(root, '.local/cleanup-plans')
        bound = False
        for original in sorted(originals.glob('*.json')):
            original = contained(root, original.relative_to(root).as_posix())
            content = original.read_bytes()
            if digest(content) == plan_sha:
                require(json.loads(content) == plan, 'Retained plan differs from the reviewed original')
                bound = True
                break
        require(bound, 'Exact reviewed plan is missing or changed; cannot verify completed cleanup')
        guards = {item['path']: item for item in plan['guards']}
        for name in ('runtime-database-duplicates.json', 'pre-relocation-build-review.json'):
            path = audit + '/' + name
            require(path in guards and snapshot(root, path)['sha256'] == guards[path]['sha256'],
                    f'Completed audit changed: {path}; create a fresh audit.')
        require(len(events) == 2 + 2 * count and events[0].get('event') == 'preflight-passed' and
                events[0].get('files') == count and events[-1].get('files') == count,
                'Incomplete cleanup journal; completion cannot be verified')
        for position, item in enumerate(plan['candidates']):
            for offset, kind in enumerate(('delete-intent', 'deleted')):
                event = events[1 + 2 * position + offset]
                require(event.get('event') == kind and event.get('path') == item['path'] and
                        (kind != 'delete-intent' or event.get('sha256') == item['sha256']),
                        'Cleanup journal does not match the reviewed removals')
        blob = f'{RETAINED}/sha256/{group["sha256"]}/f4rd-runtime.bin'
        retained = snapshot(root, blob)
        require(retained['sha256'] == group['sha256'] and retained['bytes'] == group['bytesPerFile'] and
                retained['hardlinkCount'] == 1, 'Retained runtime database failed verification')
        index_path = f'{RETAINED}/indexes/{plan_sha}.json'
        index = read_json(contained(root, index_path))
        runtime = [item for item in plan['candidates'] if item['action'] == 'retain-runtime']
        require(index.get('schema') == 1 and index.get('workspace') == str(root) and
                index.get('planSha256') == plan_sha and index.get('originalFiles') == runtime and
                index.get('selectedOriginalPaths') == [item['path'] for item in runtime] and
                all(index['retained'][key] == retained[key] for key in ('path', 'bytes', 'sha256')),
                'Retained runtime index does not match the completed cleanup')
        return dict(result='already-clean', scope='The supplied historical audit only', audit=audit,
                    previouslyRemovedFiles=count, remainingCandidates=0, removedFiles=0,
                    journal=str(journal_path), retainedRuntimeDatabase=str(root / blob),
                    retainedRuntimeSha256=retained['sha256'],
                    nextStep='No action for this completed audit. Routine repository cleanup handles outputs retention and build output; '
                             'new permanent historical removals require a fresh audit.')
    raise ValueError('Audit candidates are missing without a verified completed cleanup. '
                     'Review .local/cleanup-runs and create a fresh audit; no files were removed.')


def create_plan(root, audit, output):
    allowed, group = policy(root, audit)
    missing = [path for _, path in allowed.values() if not contained(root, path).exists()]
    if missing:
        return completed_audit(root, audit, allowed, group, missing)
    configured = configured_paths(root)
    candidates = []
    for action, path in sorted(allowed.values(), key=lambda item: item[1]):
        item = snapshot(root, path)
        require(item['hardlinkCount'] == 1, f'Hardlinked candidate rejected: {path}')
        if action == 'retain-runtime':
            require(item['sha256'] == group['sha256'] and item['bytes'] == group['bytesPerFile'],
                    f'Runtime database differs from audit: {path}')
        check_configured(root, path, configured)
        item.update(action=action, selected=False, review='', reason=(
            'Identical full runtime database; retain content-addressed bytes and original-path index first. '
            'Review replay consumers before removing this path.' if action == 'retain-runtime' else
            'Audited compiler intermediate in mixed pre-relocation tree; sources, binaries, symbols and reports excluded.'))
        candidates.append(item)
    guards = [snapshot(root, p) for p in guard_paths(root, audit)]
    runtime_bytes = group['bytesPerFile']
    plan = dict(schema=1, workspace=str(root), createdUtc=utc(), audit=audit,
                mode='review-required; no candidates selected', configuredPaths=configured,
                protected=PROTECTED, guards=guards, candidates=candidates,
                runtimeRetention=dict(sha256=group['sha256'], bytes=runtime_bytes,
                    blob=f'{RETAINED}/sha256/{group["sha256"]}/f4rd-runtime.bin',
                    originalPaths=group['paths'],
                    index=f'{RETAINED}/indexes/<reviewed-plan-sha256>.json'),
                totals=dict(runtimeCopies=group['copies'], runtimeRedundantCopies=group['copies'] - 1,
                    runtimeNetBytesIfAllOriginalsRetired=runtime_bytes * (group['copies'] - 1),
                    intermediateFiles=sum(c['action'] == 'remove-intermediate' for c in candidates),
                    intermediateBytes=sum(c['bytes'] for c in candidates if c['action'] == 'remove-intermediate')))
    require(output.startswith('.local/cleanup-plans/'), 'Reports must be new files under .local/cleanup-plans')
    path = new_json(root, output, plan)
    return dict(result='dry-run', plan=str(path), planSha256=digest(path.read_bytes()),
                selectedFiles=0, **plan['totals'])


def apply_plan(root, plan_path, reviewed_sha):
    require(reviewed_sha and len(reviewed_sha) == 64, 'Supply the reviewed plan SHA-256')
    with build_lock(root), contextlib.ExitStack() as stack:
        plan_snapshot = snapshot(root, plan_path)
        require(plan_snapshot['sha256'] == reviewed_sha.lower(), 'Reviewed plan hash mismatch')
        locked_plan = stack.enter_context(LockedFile(root, plan_snapshot))
        plan = json.load(locked_plan.stream)
        require(plan['schema'] == 1 and plan['workspace'] == str(root), 'Plan belongs to another workspace/schema')
        allowed, group = policy(root, plan['audit'])
        configured = configured_paths(root)
        require(configured == plan['configuredPaths'], 'Configured inputs changed; regenerate and review')
        require(set(guard_paths(root, plan['audit'])) == {g['path'] for g in plan['guards']},
                'Protected/consumer file set changed; regenerate and review')
        for guard in plan['guards']:
            require(snapshot(root, guard['path']) == guard, f'Protected/consumer file changed: {guard["path"]}')
        seen = set()
        selected = []
        runtime = []
        for item in plan['candidates']:
            path = item['path']
            contained(root, path)
            require(path.casefold() not in seen, f'Duplicate candidate: {path}')
            seen.add(path.casefold())
            require(allowed.get(path.casefold()) == (item['action'], path), f'Protected or unaudited path: {path}')
            require(type(item['selected']) is bool, 'selected must be a JSON boolean')
            check_configured(root, path, configured)
            if item['action'] == 'retain-runtime':
                require(item['sha256'] == group['sha256'] and item['bytes'] == group['bytesPerFile'],
                        'Invalid runtime retention identity')
                runtime.append(item)
            if item['selected']:
                require(isinstance(item['review'], str) and len(item['review'].strip()) >= 12,
                        f'An explicit disposition/consumer review is required: {path}')
                selected.append(item)
        require(seen == set(allowed), 'Plan must retain the complete audited path index')
        require(selected, 'No candidates selected; edit and review a copy of the dry-run plan')
        expected_blob = f'{RETAINED}/sha256/{group["sha256"]}/f4rd-runtime.bin'
        require(plan['runtimeRetention']['blob'] == expected_blob and
                plan['runtimeRetention']['originalPaths'] == group['paths'], 'Retention destination/index changed')
        # Lock and hash ALL selected files before the first deletion. Hold all handles
        # throughout the operation, including the source of retained database bytes.
        opened = {i['path']: stack.enter_context(LockedFile(root, i, delete=True)) for i in selected}
        retire_runtime = any(i['action'] == 'retain-runtime' for i in selected)
        run = '.local/cleanup-runs/' + uuid.uuid4().hex
        if retire_runtime:
            for item in runtime:
                if item['path'] not in opened:
                    opened[item['path']] = stack.enter_context(LockedFile(root, item))
            blob = contained(root, expected_blob)
            if not blob.exists():
                blob.parent.mkdir(parents=True, exist_ok=True)
                no_reparse(blob)
                source = opened[runtime[0]['path']].stream
                source.seek(0)
                # Exclusive creation; interrupted partial copies are never reused.
                with blob.open('xb') as target:
                    shutil.copyfileobj(source, target, 1024 * 1024)
                    target.flush()
                    os.fsync(target.fileno())
            retained = snapshot(root, expected_blob)
            require(retained['sha256'] == group['sha256'] and retained['bytes'] == group['bytesPerFile']
                    and retained['hardlinkCount'] == 1, 'Retained runtime database failed verification')
            stack.enter_context(LockedFile(root, retained))
            index = dict(schema=1, workspace=str(root), planSha256=reviewed_sha.lower(),
                         retained=retained, originalFiles=runtime,
                         selectedOriginalPaths=[i['path'] for i in selected if i['action'] == 'retain-runtime'],
                         method='Verified byte copy followed by removal of explicitly reviewed original paths; no links')
            index_path = f'{RETAINED}/indexes/{reviewed_sha.lower()}.json'
            new_json(root, index_path, index)
            stack.enter_context(LockedFile(root, snapshot(root, index_path)))
        # Keep the reviewed plan and a flushed before/after event journal. No
        # rollback claim: an I/O failure can leave a documented partial apply.
        new_json(root, run + '/reviewed-plan.json', plan)
        journal_path = contained(root, run + '/journal.jsonl')
        with journal_path.open('x', encoding='utf-8') as journal:
            def event(value):
                journal.write(json.dumps(dict(utc=utc(), **value)) + '\n')
                journal.flush()
                os.fsync(journal.fileno())
            event(dict(event='preflight-passed', planSha256=reviewed_sha, files=len(selected)))
            for item in selected:
                event(dict(event='delete-intent', path=item['path'], sha256=item['sha256']))
                opened[item['path']].delete()
                event(dict(event='deleted', path=item['path']))
            event(dict(event='complete', files=len(selected)))
        return dict(result='applied', removedFiles=len(selected), removedLogicalBytes=sum(i['bytes'] for i in selected),
                    journal=str(journal_path))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--audit', default=AUDIT)
    parser.add_argument('--plan', default='.local/cleanup-plans/' + uuid.uuid4().hex + '.json')
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--reviewed-sha256')
    args = parser.parse_args()
    root = workspace(args.root)
    if args.apply:
        result = apply_plan(root, args.plan, args.reviewed_sha256)
    else:
        require(not args.reviewed_sha256, '--reviewed-sha256 requires --apply')
        with build_lock(root):
            result = create_plan(root, args.audit, args.plan)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        print(f'Cleanup refused: {error}', file=sys.stderr)
        sys.exit(1)

# SPDX-License-Identifier: GPL-3.0-or-later
"""Inventory output cases; archive verified completed cases and restore them on request."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import time
import uuid
import zipfile

from cleanup_repository import configured_paths
from maintenance_paths import (LockedFile, build_lock, contained, digest, metadata,
                               new_json, no_reparse, read_json, require, snapshot, workspace)

POLICY = 'config/outputs-retention.json'
POINTER = '.clipboard-output-archive.json'
NOTE = 'OUTPUTS-ARCHIVE.txt'
RESERVED = {POINTER, NOTE}


def policy(root):
    value = read_json(contained(root, POLICY))
    require(value['schemaVersion'] == 1, 'Unsupported outputs retention policy')
    require(type(value['completedAfterDays']) is int and value['completedAfterDays'] >= 0, 'Invalid retention days')
    require(value['archiveRoot'] == '.local/outputs-archives' and
            value['statePath'] == '.local/outputs-retention.json', 'Unexpected retention storage location')
    for case in list(value['protected']) + value['collections']:
        case_path(root, case)
    return value


def case_path(root, case):
    require(case.startswith('outputs/') and len(case.split('/')) >= 2, 'Expected a case below outputs/')
    return contained(root, case)


def states(root, rules):
    path = contained(root, rules['statePath'])
    value = read_json(path) if path.exists() else dict(schemaVersion=1, cases={})
    require(value['schemaVersion'] == 1 and isinstance(value['cases'], dict), 'Invalid output case registry')
    for case, record in value['cases'].items():
        case_path(root, case)
        require(record['status'] in ('active', 'completed', 'archive') and record.get('reason'),
                f'Invalid case disposition: {case}')
    return value


def set_state(root, rules, case, status, reason):
    case_path(root, case)
    value = states(root, rules)
    value['cases'][case] = dict(status=status, reason=reason,
                                updatedUtc=datetime.now(timezone.utc).isoformat())
    target = contained(root, rules['statePath'])
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = new_json(root, rules['statePath'] + '.' + uuid.uuid4().hex + '.tmp', value)
    os.replace(temporary, target)


def inventory(root, case):
    base = case_path(root, case)
    rows, directories = [], []
    if not base.exists():
        return rows, directories
    require(base.is_dir(), f'Output case is not a directory: {case}')
    for folder, dirs, files in os.walk(base, followlinks=False):
        no_reparse(Path(folder))
        directories.append(Path(folder).relative_to(root).as_posix())
        for name in dirs:
            no_reparse(Path(folder) / name)
        for name in sorted(files):
            path = Path(folder) / name
            no_reparse(path)
            if path.parent == base and name in RESERVED:
                continue
            rows.append(dict(path=path.relative_to(root).as_posix(), **metadata(path.stat())))
    return sorted(rows, key=lambda row: row['path']), sorted(directories)


def cases(root, rules):
    base = contained(root, 'outputs')
    result, loose = [], []
    if not base.exists():
        return result, loose
    for path in sorted(base.iterdir()):
        no_reparse(path)
        relative = path.relative_to(root).as_posix()
        if path.is_file():
            loose.append(dict(path=relative, bytes=path.stat().st_size))
        elif relative in rules['collections']:
            for child in sorted(path.iterdir()):
                no_reparse(child)
                if child.is_dir():
                    result.append(child.relative_to(root).as_posix())
                else:
                    loose.append(dict(path=child.relative_to(root).as_posix(), bytes=child.stat().st_size))
        else:
            result.append(relative)
    return result, loose


def protection(root, rules, case, inputs):
    for prefix, reason in rules['protected'].items():
        if case == prefix or case.startswith(prefix + '/') or prefix.startswith(case + '/'):
            return reason
    absolute = os.path.normcase(str(case_path(root, case)))
    for value in inputs:
        if absolute == value or absolute.startswith(value.rstrip('\\/') + os.sep) or value.startswith(absolute + os.sep):
            return 'Contains or lies inside a currently configured build input'
    return None


def classify(root, rules, registry, case, inputs, now):
    rows, _ = inventory(root, case)
    record = registry['cases'].get(case, {})
    reason = protection(root, rules, case, inputs)
    item = dict(case=case, files=len(rows), bytes=sum(row['bytes'] for row in rows), action='keep')
    if reason:
        item['reason'] = reason
    elif any(row['hardlinkCount'] != 1 for row in rows):
        item['reason'] = 'Hardlinked file: requires a separate review'
    elif record.get('status') == 'active':
        item['reason'] = record['reason']
    elif (case_path(root, case) / POINTER).exists():
        manifest, manifest_path = load_archive(root, rules, case)
        removable = [row for row in rows if row['path'] not in manifest['keptPaths']]
        item.update(action='finish-archive' if removable else 'archived',
                    reason='Archive recorded; apply verifies its contents before completing removal',
                    removableBytes=sum(row['bytes'] for row in removable))
        if {row['path'] for row in rows} == {row['path'] for row in manifest['files']} and removable and item['removableBytes'] <= manifest['archive']['bytes'] + manifest_path.stat().st_size:
            item.update(action='keep', reason='Archive would not save space; original case remains unpacked')
    elif not rows:
        item['reason'] = 'Empty case'
    elif record.get('status') not in ('completed', 'archive'):
        item['reason'] = 'Unresolved or unclassified case; keep until explicitly completed'
    elif record['status'] == 'completed' and now - max(row['mtimeNs'] for row in rows) / 1e9 < rules['completedAfterDays'] * 86400:
        item['reason'] = f'Completed, but changed within the last {rules["completedAfterDays"]} days'
    else:
        item.update(action='archive', reason=record['reason'])
    return item


def report(root, rules, registry):
    found, loose = cases(root, rules)
    inputs = configured_paths(root)
    items = [classify(root, rules, registry, case, inputs, time.time()) for case in found]
    eligible = [item for item in items if item['action'] in ('archive', 'finish-archive')]
    return dict(result='dry-run' if eligible else 'retained', scope='outputs retention',
                totalFiles=sum(item['files'] for item in items) + len(loose),
                totalBytes=sum(item['bytes'] for item in items) + sum(row['bytes'] for row in loose),
                eligibleCases=len(eligible), eligibleUnpackedBytes=sum(item['bytes'] for item in eligible),
                note='Archive compression determines actual savings. Unresolved, active and unknown cases stay unpacked.',
                cases=items, retainedLooseFiles=loose)


def keep_reports(rows, rules):
    kept, used = [], 0
    for row in rows:
        path = Path(row['path'])
        if (path.suffix.lower() in rules['keepReportExtensions'] or path.name.lower() in rules['keepReportNames']) and row['bytes'] <= rules['maxReportBytes']:
            if used + row['bytes'] <= rules['maxKeptReportBytesPerCase']:
                kept.append(row['path'])
                used += row['bytes']
    return kept


def load_archive(root, rules, case):
    pointer = read_json(contained(root, case + '/' + POINTER))
    relative = pointer['manifest']
    require(relative.startswith(rules['archiveRoot'] + '/') and relative.endswith('/manifest.json'), 'Invalid archive manifest location')
    manifest_path = contained(root, relative)
    require(snapshot(root, relative)['sha256'] == pointer['manifestSha256'], 'Archive manifest changed')
    manifest = read_json(manifest_path)
    require(manifest['schemaVersion'] == 1 and manifest['case'] == case, 'Archive belongs to another output case')
    require(manifest['archive']['path'] == relative.removesuffix('manifest.json') + 'case.zip', 'Invalid ZIP location')
    paths = set()
    for row in manifest['files']:
        require(row['path'].startswith(case + '/') and row['path'].casefold() not in paths, 'Archive file escaped or duplicated its case')
        contained(root, row['path'])
        paths.add(row['path'].casefold())
        require(len(row['sha256']) == 64 and all(c in '0123456789abcdef' for c in row['sha256']), 'Invalid content hash')
    require(set(manifest['keptPaths']) <= {row['path'] for row in manifest['files']}, 'Invalid retained-report inventory')
    for directory in manifest['directories']:
        require(directory == case or directory.startswith(case + '/'), 'Archive directory escaped case')
        contained(root, directory)
    return manifest, manifest_path


def verify_zip(manifest, stream):
    blobs = {row['sha256']: row['bytes'] for row in manifest['files']}
    require(all(blobs[row['sha256']] == row['bytes'] for row in manifest['files']), 'Inconsistent archive content sizes')
    with zipfile.ZipFile(stream) as archive:
        names = archive.namelist()
        require(len(names) == len(set(names)) and set(names) == {'blobs/' + sha for sha in blobs}, 'Archive ZIP inventory mismatch')
        for sha, size in blobs.items():
            require(archive.getinfo('blobs/' + sha).file_size == size, 'Archive entry size mismatch')
            value = hashlib.sha256()
            with archive.open('blobs/' + sha) as source:
                for block in iter(lambda: source.read(1024 * 1024), b''):
                    value.update(block)
            require(value.hexdigest() == sha, 'Archive entry SHA-256 mismatch')


def prepare_archive(root, rules, case):
    rows, directories = inventory(root, case)
    require(rows, 'No case files to archive')
    require(not (case_path(root, case) / POINTER).exists() and not (case_path(root, case) / NOTE).exists(), 'Reserved retention file already exists')
    files = []
    for row in rows:
        item = snapshot(root, row['path'])
        require(all(item[key] == row[key] for key in row) and item['hardlinkCount'] == 1, 'Case changed during inventory')
        files.append(item)
    identity = digest(json.dumps([(row['path'], row['sha256']) for row in files]).encode())
    destination = rules['archiveRoot'] + '/' + identity
    zip_path = contained(root, destination + '/case.zip')
    existing_manifest = contained(root, destination + '/manifest.json')
    if existing_manifest.exists():
        manifest = read_json(existing_manifest)
        require(manifest['case'] == case and [(row['path'], row['sha256']) for row in manifest['files']] ==
                [(row['path'], row['sha256']) for row in files], 'Existing archive identity mismatch')
        require(manifest['archive']['path'] == destination + '/case.zip', 'Existing archive path mismatch')
        with LockedFile(root, manifest['archive']) as saved:
            verify_zip(manifest, saved.stream)
        publish_pointer(root, case, existing_manifest)
        return manifest
    if zip_path.parent.exists():
        # Preserve interrupted generation for diagnosis, using only checked
        # workspace paths. Original output files have not been removed yet.
        old = contained(root, destination)
        quarantined = contained(root, destination + '.incomplete-' + uuid.uuid4().hex)
        no_reparse(old)
        os.rename(old, quarantined)
    zip_path.parent.mkdir(parents=True)
    unique = {}
    with zipfile.ZipFile(zip_path, 'x', zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as archive:
        for row in files:
            with LockedFile(root, row) as source:
                if row['sha256'] not in unique:
                    with archive.open('blobs/' + row['sha256'], 'w', force_zip64=True) as target:
                        shutil.copyfileobj(source.stream, target, 1024 * 1024)
                    unique[row['sha256']] = row['bytes']
    manifest = dict(schemaVersion=1, case=case, createdUtc=datetime.now(timezone.utc).isoformat(),
                    files=files, directories=directories, keptPaths=keep_reports(files, rules),
                    deduplicatedBytes=sum(row['bytes'] for row in files) - sum(unique.values()),
                    archive=snapshot(root, destination + '/case.zip'))
    with LockedFile(root, manifest['archive']) as saved:
        verify_zip(manifest, saved.stream)
    manifest_path = new_json(root, destination + '/manifest.json', manifest)
    publish_pointer(root, case, manifest_path)
    return manifest


def publish_pointer(root, case, manifest_path):
    # Durable pointer precedes removal. An interrupted apply resumes from these
    # verified bytes; it never makes a new archive from a partially pruned case.
    new_json(root, case + '/' + POINTER,
             dict(schemaVersion=1, manifest=manifest_path.relative_to(root).as_posix(),
                  manifestSha256=snapshot(root, manifest_path.relative_to(root).as_posix())['sha256']))
    note = contained(root, case + '/' + NOTE)
    with note.open('x', encoding='utf-8') as stream:
        stream.write(f'Case archive: {manifest_path.relative_to(root).as_posix()}\n'
                     f'Restore from the repository root:\n'
                     f'pwsh -NoProfile -File .\\tools\\maintenance\\Clean-ClipboardRepository.ps1 -RestoreOutputCase "{case}"\n'
                     'The manifest maps original paths and hashes to verified ZIP contents. Concise reports remain here.\n')


def prune_archive(root, rules, case):
    manifest, manifest_path = load_archive(root, rules, case)
    expected = {row['path']: row for row in manifest['files']}
    rows, _ = inventory(root, case)
    current = {row['path'] for row in rows}
    require(current <= set(expected), 'New files appeared in archived case; restore/review before cleanup')
    require(set(manifest['keptPaths']) <= current, 'Retained analysis report is missing')
    with LockedFile(root, manifest['archive']) as saved:
        verify_zip(manifest, saved.stream)
        # Check the whole surviving inventory before the first deletion. Per-file
        # locks then prevent replacing a checked filename during its deletion.
        verified = []
        for row in rows:
            current_row = snapshot(root, row['path'])
            require(all(current_row[key] == row[key] for key in row) and
                    all(current_row[key] == expected[row['path']][key] for key in ('bytes', 'sha256')),
                    'Case changed after archival: ' + row['path'])
            verified.append(current_row)
        remove = [row for row in verified if row['path'] not in manifest['keptPaths']]
        removable_bytes = sum(row['bytes'] for row in remove)
        if not remove:
            return dict(case=case, result='already-archived', removedFiles=0, removedBytes=0)
        # Initial savings include storage cost; a partially applied case resumes
        # even when its remaining files alone are smaller than the retained ZIP.
        initial = current == set(expected)
        if initial and removable_bytes <= manifest['archive']['bytes'] + manifest_path.stat().st_size:
            return dict(case=case, result='kept-no-space-saving', removedFiles=0, removedBytes=0)
        journal = manifest_path.parent / ('prune-' + uuid.uuid4().hex + '.jsonl')
        with journal.open('x', encoding='utf-8') as log:
            def event(kind, path):
                log.write(json.dumps(dict(event=kind, path=path)) + '\n')
                log.flush()
                os.fsync(log.fileno())
            for row in remove:
                with LockedFile(root, row, delete=True) as source:
                    event('delete-intent', row['path'])
                    source.delete()
                    event('deleted', row['path'])
            event('complete', case)
    # Remove only checked, now-empty directories. Never recursively delete an
    # output tree; newly created files keep their containing directories alive.
    for directory in sorted(manifest['directories'], key=len, reverse=True):
        if directory != case:
            path = contained(root, directory)
            if path.is_dir() and not any(path.iterdir()):
                path.rmdir()
    return dict(case=case, result='archived', removedFiles=len(remove), removedBytes=removable_bytes,
                archiveBytes=manifest['archive']['bytes'], deduplicatedBytes=manifest['deduplicatedBytes'],
                manifest=str(manifest_path), journal=str(journal))


def apply(root, rules, registry):
    before = report(root, rules, registry)
    operations = []
    guard = snapshot(root, POLICY)
    registry_path = contained(root, rules['statePath'])
    registry_guard = snapshot(root, rules['statePath']) if registry_path.exists() else None
    for item in before['cases']:
        if item['action'] not in ('archive', 'finish-archive'):
            continue
        case = item['case']
        require(snapshot(root, POLICY) == guard, 'Retention policy changed during cleanup')
        require((snapshot(root, rules['statePath']) if registry_path.exists() else None) == registry_guard, 'Case status changed during cleanup')
        require(not protection(root, rules, case, configured_paths(root)), 'Case became an active input')
        print(f'Archiving output case: {case}', file=sys.stderr, flush=True)
        if item['action'] == 'archive':
            prepare_archive(root, rules, case)
        require(snapshot(root, POLICY) == guard and
                (snapshot(root, rules['statePath']) if registry_path.exists() else None) == registry_guard,
                'Retention decisions changed while archiving; originals preserved')
        require(not protection(root, rules, case, configured_paths(root)), 'Case became an active input')
        operations.append(prune_archive(root, rules, case))
    return dict(result='cleaned' if any(row['removedFiles'] for row in operations) else 'retained',
                scope='outputs retention', operations=operations,
                removedFiles=sum(row['removedFiles'] for row in operations),
                removedBytes=sum(row['removedBytes'] for row in operations),
                totalBytes=before['totalBytes'] - sum(row['removedBytes'] for row in operations),
                eligibleCases=0,
                retainedCases=[row for row in before['cases'] if row['action'] not in ('archive', 'finish-archive')])


def restore(root, rules, case):
    manifest, manifest_path = load_archive(root, rules, case)
    require(not protection(root, rules, case, configured_paths(root)), 'Cannot restore over an active fixture/input')
    with LockedFile(root, manifest['archive']) as saved:
        verify_zip(manifest, saved.stream)
        for row in manifest['files']:
            path = contained(root, row['path'])
            if path.exists():
                require(snapshot(root, row['path'])['sha256'] == row['sha256'], 'Restore would overwrite changed file: ' + row['path'])
        restored = 0
        with zipfile.ZipFile(saved.stream) as archive:
            for directory in manifest['directories']:
                contained(root, directory).mkdir(parents=True, exist_ok=True)
            for row in manifest['files']:
                path = contained(root, row['path'])
                if path.exists():
                    continue
                path.parent.mkdir(parents=True, exist_ok=True)
                temporary = contained(root, row['path'] + '.restore-' + uuid.uuid4().hex + '.tmp')
                with temporary.open('xb') as target, archive.open('blobs/' + row['sha256']) as source:
                    shutil.copyfileobj(source, target, 1024 * 1024)
                    target.flush()
                    os.fsync(target.fileno())
                require(snapshot(root, temporary.relative_to(root).as_posix())['sha256'] == row['sha256'], 'Restored file hash mismatch')
                # Windows rename fails if the target appeared; never replace it.
                os.rename(temporary, path)
                os.utime(path, ns=(row['mtimeNs'], row['mtimeNs']))
                restored += 1
    set_state(root, rules, case, 'active', 'Restored for investigation; protect until explicitly completed again')
    return dict(result='restored', case=case, files=restored, manifest=str(manifest_path), status='active')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument('--apply', action='store_true')
    modes.add_argument('--restore')
    modes.add_argument('--complete')
    modes.add_argument('--keep')
    args = parser.parse_args()
    root = workspace(args.root)
    with build_lock(root):
        rules = policy(root)
        if args.restore:
            result = restore(root, rules, args.restore)
        elif args.complete or args.keep:
            case = args.complete or args.keep
            require(case in cases(root, rules)[0], 'Select an existing complete output case')
            set_state(root, rules, case, 'completed' if args.complete else 'active', 'Explicit case status set through cleanup command')
            result = dict(result='case-status-updated', case=case, status='completed' if args.complete else 'active')
        else:
            registry = states(root, rules)
            result = apply(root, rules, registry) if args.apply else report(root, rules, registry)
    report_path = new_json(root, '.local/outputs-retention-reports/' + uuid.uuid4().hex + '.json', result)
    summary = {key: value for key, value in result.items() if key not in ('cases', 'retainedCases', 'retainedLooseFiles')}
    rows = result.get('cases', result.get('retainedCases', []))
    summary['largestCases'] = sorted(rows, key=lambda row: row['bytes'], reverse=True)[:8]
    summary['report'] = str(report_path)
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError, zipfile.BadZipFile) as error:
        print(f'Outputs cleanup refused: {error}', file=sys.stderr)
        sys.exit(1)

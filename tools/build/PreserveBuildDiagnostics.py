# SPDX-License-Identifier: GPL-3.0-or-later
"""Retain a verified package's symbols and build records outside disposable build/."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import uuid


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def safe(root, path):
    path = Path(os.path.abspath(path if Path(path).is_absolute() else root / path))
    relative = path.relative_to(root)
    current = root
    for part in relative.parts:
        current /= part
        if current.exists() and (current.is_symlink() or
                getattr(current.lstat(), 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT):
            raise ValueError(f'Reparse point is not allowed: {current}')
    return path


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def preserve(root, record_path):
    root = Path(os.path.abspath(root))
    record_path = safe(root, record_path)
    record = read(record_path)
    number = record['internalBuild']
    if type(number) is not int or number < 100 or record['result'] != 'success':
        raise ValueError('Expected a successful numbered publication record')
    native_path = safe(root, record['nativeBuild']['metadata'])
    papyrus_path = safe(root, record['papyrusBuild']['metadata'])
    for path, expected in ((native_path, record['nativeBuild']['metadataSha256']),
                           (papyrus_path, record['papyrusBuild']['metadataSha256'])):
        if sha(path) != expected.lower():
            raise ValueError(f'Publication metadata hash mismatch: {path}')
    native, papyrus = read(native_path), read(papyrus_path)
    if any(r['internalBuild'] != number or r['result'] != 'success' for r in (native, papyrus)):
        raise ValueError('Native/Papyrus identity does not match publication')
    selected = {native_path, papyrus_path}
    for component in ('dll', 'pdb'):
        item = native[component]
        path = safe(root, item['path'])
        if sha(path) != item['sha256'].lower():
            raise ValueError(f'{component} hash does not match native build record')
        selected.add(path)
    if native['dll']['sha256'].lower() != record['nativeBuild']['dllSha256'].lower():
        raise ValueError('DLL differs from publication')
    if len(papyrus['outputs']) != 16:
        raise ValueError('Expected 16 compiled Papyrus outputs')
    for item in papyrus['outputs']:
        if Path(item['file']).name != item['file']:
            raise ValueError('Invalid Papyrus output filename')
        path = safe(root, 'build/papyrus/current/Scripts/' + item['file'])
        if sha(path) != item['sha256'].lower():
            raise ValueError('Papyrus output hash differs from its build record')
        selected.add(path)
    # These records and logs are small compared with native intermediates. Keep
    # their original relative paths without rewriting historical attestations.
    for relative in ('build/metadata', 'build/logs', 'build/interface', 'build/papyrus/current/Scripts'):
        folder = safe(root, relative)
        if folder.exists():
            for directory, dirs, files in os.walk(folder, followlinks=False):
                for name in dirs + files:
                    safe(root, Path(directory) / name)
                selected.update(Path(directory) / name for name in files)
    entries = [{'path': p.relative_to(root).as_posix(), 'bytes': p.stat().st_size, 'sha256': sha(p)}
               for p in sorted(selected)]
    identity = hashlib.sha256(json.dumps(entries, sort_keys=True).encode()).hexdigest()
    parent = safe(root, f'.local/build-history/build-{number}')
    destination = safe(root, parent / identity)
    manifest = {'schemaVersion': 1, 'internalBuild': number, 'archiveSha256': record.get('archiveSha256'),
                'originalPublicationRecord': record_path.relative_to(root).as_posix(),
                'dllSha256': native['dll']['sha256'], 'pdbSha256': native['pdb']['sha256'],
                'files': entries, 'scope': 'Retained original bytes and paths; not an active input or new build'}
    if not destination.exists():
        pending = safe(root, parent / ('.pending-' + uuid.uuid4().hex))
        pending.mkdir(parents=True)
        for entry in entries:
            source = safe(root, entry['path'])
            target = safe(root, pending / entry['path'])
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            if sha(source) != entry['sha256'] or sha(target) != entry['sha256']:
                raise ValueError(f'Changed while retaining diagnostics: {source}')
        (pending / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        pending.rename(destination)
    saved = read(destination / 'manifest.json')
    if saved['files'] != entries:
        raise ValueError('Existing retained snapshot differs')
    for entry in entries:
        if sha(safe(root, destination / entry['path'])) != entry['sha256']:
            raise ValueError('Retained diagnostic file failed verification')
    return {'manifest': str(destination / 'manifest.json'), 'manifestSha256': sha(destination / 'manifest.json'),
            'internalBuild': number, 'fileCount': len(entries), 'result': 'passed'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--record', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(preserve(args.root, args.record)))

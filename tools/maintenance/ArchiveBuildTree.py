# SPDX-License-Identifier: GPL-3.0-or-later
"""Archive and verify all remaining build files before an explicitly requested clean."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import uuid
import zipfile

from maintenance_paths import contained, new_json, no_reparse, snapshot, workspace


def archive(root):
    root = workspace(root)
    source = contained(root, 'build')
    if not source.is_dir():
        raise ValueError('No build directory to archive')
    entries = []
    directories = []
    for folder, dirs, files in os.walk(source, followlinks=False):
        directories.append(Path(folder).relative_to(root).as_posix())
        for name in dirs + files:
            no_reparse(Path(folder)/name)
        for name in sorted(files):
            entries.append(snapshot(root, (Path(folder)/name).relative_to(root).as_posix()))
    destination = '.local/build-history/clean-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + uuid.uuid4().hex[:8]
    archive_path = contained(root, destination + '/build.zip')
    archive_path.parent.mkdir(parents=True)
    with zipfile.ZipFile(archive_path, 'x', zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as z:
        for entry in entries:
            z.write(contained(root, entry['path']), entry['path'])
    with zipfile.ZipFile(archive_path) as z:
        if set(z.namelist()) != {e['path'] for e in entries}:
            raise ValueError('Build archive inventory mismatch')
        for entry in entries:
            digest = hashlib.sha256()
            with z.open(entry['path']) as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b''):
                    digest.update(block)
            if digest.hexdigest() != entry['sha256'] or snapshot(root, entry['path']) != entry:
                raise ValueError('Build changed while archiving: ' + entry['path'])
    manifest = {'schemaVersion': 1, 'root': str(root), 'scope': 'Complete pre-clean snapshot; no source deletion by this helper',
                'files': entries, 'directories': directories,
                'archive': snapshot(root, archive_path.relative_to(root).as_posix())}
    manifest_path = new_json(root, destination + '/manifest.json', manifest)
    return {'result': 'passed', 'manifest': str(manifest_path), 'archive': str(archive_path),
            'files': len(entries), 'sourceBytes': sum(e['bytes'] for e in entries), 'archiveBytes': archive_path.stat().st_size}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(archive(args.root)))

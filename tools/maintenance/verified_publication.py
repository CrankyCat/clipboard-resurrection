# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only verification of a numbered dist publication and its full stage."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import zipfile

from maintenance_paths import (as_relative, build_lock, contained, no_reparse,
                               read_json, relative_path, require, snapshot, workspace)


def verified_publication(root, version, build=None):
    dist = contained(root, 'dist')
    require(dist.is_dir(), 'No numbered dist publication is available')
    numbers = []
    for path in dist.iterdir():
        if re.fullmatch(r'build-[1-9][0-9]*', path.name):
            no_reparse(path)
            if path.is_dir():
                numbers.append(int(path.name[6:]))
    require(numbers, 'No numbered dist publication is available')
    number = max(numbers) if build is None else build
    require(number in numbers, f'Publication build {number} was not found')
    base = contained(root, f'dist/build-{number}')
    records = list(base.glob('*.build.json'))
    require(len(records) == 1, f'Expected one unambiguous build record: {base}')
    record_path = records[0]
    record_snapshot = snapshot(root, as_relative(root, record_path))
    record = read_json(record_path)
    require(record['result'] == 'success' and record['internalBuild'] == number and
            record['productVersion'] == version and record['archiveVerification']['result'] == 'passed',
            'Publication build/result/product version/verification mismatch')

    def child(field):
        path = Path(record[field])
        require(path.is_absolute() and path.parent == base, f'Invalid publication {field} path: {path}')
        contained(root, as_relative(root, path))
        return path

    stage, archive, manifest = (child(field) for field in ('stage', 'archive', 'manifest'))
    require(stage.is_dir() and archive.suffix.lower() == '.zip', 'Publication stage/archive missing or invalid')
    for field, path in (('archive', archive), ('manifest', manifest)):
        require(snapshot(root, as_relative(root, path))['sha256'] == record[field + 'Sha256'].lower(),
                f'Publication {field} hash mismatch')
    require(record['archiveVerification']['archiveSha256'].lower() == record['archiveSha256'].lower(),
            'Archive verification identity mismatch')
    files = {}
    folded = set()
    for line in manifest.read_text(encoding='utf-8-sig').splitlines():
        match = re.fullmatch(r'([0-9a-fA-F]{64}) \*(.+)', line)
        require(match is not None, 'Invalid publication manifest line')
        sha, relative = match.groups()
        relative_path(relative)
        require(relative.casefold() not in folded, 'Duplicate publication manifest path')
        folded.add(relative.casefold())
        files[relative] = sha.lower()
    require(len(files) == record['fileCount'] == record['archiveVerification']['fileCount'] and files,
            'Publication file count mismatch')
    # Verify all staged files, including absence of extras and reparse points.
    observed = set()
    import os
    for directory, dirs, names in os.walk(stage, followlinks=False):
        for name in dirs + names:
            no_reparse(Path(directory) / name)
        for name in names:
            path = Path(directory) / name
            relative = path.relative_to(stage).as_posix()
            require(relative in files and snapshot(root, as_relative(root, path))['sha256'] == files[relative],
                    f'Staged file differs from verified manifest: {relative}')
            observed.add(relative)
    require(observed == set(files), 'Staged publication has missing files')
    with zipfile.ZipFile(archive) as zipped:
        infos = [i for i in zipped.infolist() if not i.is_dir()]
        require(len(infos) == len(files) and {i.filename for i in infos} == set(files),
                'Archive paths differ from verified manifest')
        for info in infos:
            sha = hashlib.sha256()
            with zipped.open(info) as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b''):
                    sha.update(block)
            require(sha.hexdigest() == files[info.filename], f'Archive file differs: {info.filename}')
    require(snapshot(root, as_relative(root, record_path)) == record_snapshot, 'Build record changed during verification')
    return dict(result='passed', internalBuild=number, productVersion=version, stage=str(stage),
                record=str(record_path), recordSha256=record_snapshot['sha256'], archive=str(archive),
                archiveSha256=record['archiveSha256'], manifest=str(manifest),
                manifestSha256=record['manifestSha256'], fileCount=len(files))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--product-version', required=True)
    parser.add_argument('--build', type=int)
    args = parser.parse_args()
    root = workspace(args.root)
    with build_lock(root):
        result = verified_publication(root, args.product_version, args.build)
    print(json.dumps(result))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError, zipfile.BadZipFile) as error:
        print(f'Publication refused: {error}', file=sys.stderr)
        sys.exit(1)

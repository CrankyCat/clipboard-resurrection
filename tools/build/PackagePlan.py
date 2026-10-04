"""Resolve the local package recipe and independently verify archive bytes."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[2]


def relative(value: str, *, empty: bool = False) -> str:
    if empty and value == "":
        return value
    if (not isinstance(value, str) or not value or "\\" in value or
            any(part in ("", ".", "..") for part in value.split("/")) or
            re.search(r'[<>:"|?*\x00-\x1f]', value) or value.startswith("/")):
        raise ValueError(f"Unsafe package path: {value!r}")
    for part in value.split("/"):
        if part.endswith((" ", ".")) or re.fullmatch(
                r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", part, re.I):
            raise ValueError(f"Invalid Windows package path: {value!r}")
    return value


def project_path(root: Path, name: str) -> Path:
    path = root / relative(name)
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError(f"Package input escapes project: {name}")
    # Reject junctions/symlinks even when their target happens to be local.
    for item in (path, *path.parents):
        if item == root:
            break
        if item.is_symlink() or (hasattr(item, "is_junction") and item.is_junction()):
            raise ValueError(f"Package input is a link: {name}")
    return path


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def plan(root: Path, recipe_path: Path) -> dict:
    recipe = json.loads(recipe_path.read_text(encoding="utf-8-sig"))
    if recipe["schema"] != 1:
        raise ValueError("Unsupported package recipe schema")
    records, destinations = [], set()

    def add(source: str | None, destination: str):
        destination = relative(destination)
        if Path(destination).name.lower() == "pattern.ini":
            raise ValueError("Player patterns cannot be packaged")
        key = destination.casefold()
        if key in destinations:
            raise ValueError(f"Package destination collision: {destination}")
        # Files must not double as directories, including across mappings.
        if any(key.startswith(old + "/") or old.startswith(key + "/") for old in destinations):
            raise ValueError(f"Package file/directory collision: {destination}")
        destinations.add(key)
        data = b"" if source is None else project_path(root, source).read_bytes()
        records.append(dict(relativePath=destination, source=source, bytes=len(data), sha256=digest(data)))

    for tree in recipe["trees"]:
        source = relative(tree["source"])
        destination = relative(tree["destination"], empty=True)
        directory = project_path(root, source)
        if not directory.is_dir():
            raise ValueError(f"Missing package input directory: {source}")
        files = sorted(p.relative_to(directory).as_posix() for p in directory.rglob("*") if p.is_file())
        if "files" in tree and files != sorted(tree["files"]):
            raise ValueError(f"Package input inventory differs from recipe: {source}")
        if "extension" in tree and any(Path(p).suffix != tree["extension"] for p in files):
            raise ValueError(f"Unexpected file type in package input: {source}")
        if "count" in tree and len(files) != tree["count"]:
            raise ValueError(f"Package input count differs from recipe: {source}")
        for name in files:
            add(f"{source}/{name}", f"{destination}/{name}" if destination else name)
    for item in recipe["files"]:
        add(item["source"], item["destination"])
    notices = recipe["dependencyNotices"]
    directory = project_path(root, notices["source"])
    actual = sorted(p.parent.name for p in directory.glob("*/copyright"))
    if actual != sorted(notices["packages"]):
        raise ValueError("Dependency license inventory differs from recipe")
    for package in notices["packages"]:
        relative(package)
        add(f"{notices['source']}/{package}/copyright", f"{notices['destination']}/vcpkg-{package}.txt")
    slots = recipe["patternSlots"]
    if (type(slots["first"]) is not int or type(slots["last"]) is not int or
            not 1 <= slots["first"] <= slots["last"] <= 500):
        raise ValueError("Invalid package slot range")
    for number in range(slots["first"], slots["last"] + 1):
        add(None, f"{slots['destination']}/{number}/{slots['filename']}")
    return dict(schema=1, recipeSha256=digest(recipe_path.read_bytes()),
                files=sorted(records, key=lambda row: row["relativePath"]))


def verify_archive(archive: Path, records: list[dict]) -> dict:
    expected = {row["relativePath"]: row for row in records}
    with ZipFile(archive) as zipped:
        entries = zipped.infolist()
        if len(entries) != len(expected) or {item.filename for item in entries} != set(expected):
            raise ValueError("Archive inventory differs from verified package plan")
        for entry in entries:
            row = expected[entry.filename]
            data = zipped.read(entry)  # Also checks the ZIP CRC.
            if len(data) != row["bytes"] or digest(data) != row["sha256"]:
                raise ValueError(f"Archive bytes differ from verified input: {entry.filename}")
    return dict(result="passed", fileCount=len(expected), archiveSha256=digest(archive.read_bytes()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--recipe", type=Path)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--plan", type=Path)
    args = parser.parse_args()
    if args.archive:
        if not args.plan:
            parser.error("--archive requires --plan")
        result = verify_archive(args.archive, json.loads(args.plan.read_text(encoding="utf-8-sig"))["files"])
    else:
        result = plan(args.root.resolve(), args.recipe or args.root / "config/package.json")
    print(json.dumps(result, ensure_ascii=True, indent=2))


if __name__ == "__main__":
    main()

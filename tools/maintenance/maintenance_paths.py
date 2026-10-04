# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail-closed Windows file and build-lock primitives for manual maintenance."""
import contextlib
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import stat


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def hash_stream(stream):
    stream.seek(0)
    result = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b''):
        result.update(block)
    stream.seek(0)
    return result.hexdigest()


def no_reparse(path):
    # Check ancestors including the workspace itself, without resolving links first.
    for part in reversed((path, *path.parents)):
        if not os.path.lexists(part):
            continue
        info = part.lstat()
        require(not stat.S_ISLNK(info.st_mode) and not
                (getattr(info, 'st_file_attributes', 0) & 0x400),
                f'Reparse point rejected: {part}')


def workspace(root):
    root = Path(os.path.abspath(root))
    no_reparse(root)
    require(root.is_dir(), f'Workspace not found: {root}')
    return root


def relative_path(value):
    require(isinstance(value, str) and value and '\\' not in value,
            f'Use a nonempty workspace-relative forward-slash path: {value!r}')
    parts = value.split('/')
    for part in parts:
        require(part not in ('', '.', '..') and part == part.rstrip(' .') and
                not re.search(r'[<>:"|?*\x00-\x1f]', part) and
                not re.match(r'(?i)^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)', part),
                f'Unsafe path component: {value!r}')
    return value


def contained(root, relative):
    relative_path(relative)
    path = root.joinpath(*relative.split('/'))
    require(os.path.commonpath((root, path)) == str(root) and path != root,
            f'Outside workspace: {relative}')
    no_reparse(path)
    return path


def as_relative(root, path):
    path = Path(os.path.abspath(path))
    require(os.path.commonpath((root, path)) == str(root), f'Outside workspace: {path}')
    value = path.relative_to(root).as_posix()
    contained(root, value)
    return value


def metadata(info):
    require(stat.S_ISREG(info.st_mode), 'Expected a regular file')
    return dict(bytes=info.st_size, mtimeNs=info.st_mtime_ns,
                fileId=f'{info.st_dev}:{info.st_ino}', hardlinkCount=info.st_nlink)


def snapshot(root, relative):
    path = contained(root, relative)
    before = metadata(path.stat())
    with path.open('rb') as stream:
        require(metadata(os.fstat(stream.fileno())) == before, f'File changed: {relative}')
        sha = hash_stream(stream)
        require(metadata(os.fstat(stream.fileno())) == before, f'File changed: {relative}')
    no_reparse(path)
    require(metadata(path.stat()) == before, f'File changed: {relative}')
    return dict(path=relative, **before, sha256=sha)


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def new_json(root, relative, value):
    path = contained(root, relative)
    path.parent.mkdir(parents=True, exist_ok=True)
    no_reparse(path)
    with path.open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, indent=2, ensure_ascii=False)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    return path


def kernel():
    require(os.name == 'nt', 'Apply and publication lookup require Windows')
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    k.CreateMutexW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.LPCWSTR]
    k.CreateMutexW.restype = wintypes.HANDLE
    k.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    k.WaitForSingleObject.restype = wintypes.DWORD
    k.ReleaseMutex.argtypes = [wintypes.HANDLE]
    k.CloseHandle.argtypes = [wintypes.HANDLE]
    k.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                            ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    k.CreateFileW.restype = wintypes.HANDLE
    k.GetFinalPathNameByHandleW.argtypes = [wintypes.HANDLE, wintypes.LPWSTR,
                                         wintypes.DWORD, wintypes.DWORD]
    k.SetFileInformationByHandle.argtypes = [wintypes.HANDLE, ctypes.c_int,
                                           ctypes.c_void_p, wintypes.DWORD]
    return k


@contextlib.contextmanager
def build_lock(root):
    k = kernel()
    # Exact name used by Enter-ClipboardBuildLock in ClipboardBuildIdentity.ps1.
    name = 'Local\\ClipboardBuild_' + digest(str(root).lower().encode('utf-8')).upper()
    handle = k.CreateMutexW(None, False, name)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    entered = False
    try:
        result = k.WaitForSingleObject(handle, 0)
        require(result in (0, 0x80), 'Another Clipboard build/publication/cleanup is running')
        entered = True
        yield
    finally:
        if entered:
            k.ReleaseMutex(handle)
        k.CloseHandle(handle)


class LockedFile:
    """No write/delete sharing; deletion applies to the verified handle, not a name."""
    def __init__(self, root, expected, delete=False):
        import msvcrt
        self.stream = None
        self.k = kernel()
        self.path = contained(root, expected['path'])
        handle = self.k.CreateFileW(str(self.path), 0x80000000 | (0x10000 if delete else 0),
                                   0, None, 3, 0x00200000, None)
        if handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        self.handle = handle
        try:
            final = ctypes.create_unicode_buffer(32768)
            require(self.k.GetFinalPathNameByHandleW(handle, final, len(final), 0),
                    f'Cannot resolve opened file: {self.path}')
            actual = final.value.removeprefix('\\\\?\\')
            require(os.path.normcase(actual) == os.path.normcase(str(self.path)),
                    f'Opened file escaped its expected path: {self.path}')
            no_reparse(self.path)
            self.stream = os.fdopen(msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY), 'rb')
            handle = None  # fd now owns it
            observed = dict(path=expected['path'], **metadata(os.fstat(self.stream.fileno())),
                            sha256=hash_stream(self.stream))
            require(all(observed[key] == expected[key] for key in observed),
                    f'Changed file/hash/identity: {self.path}')
            if delete:
                require(observed['hardlinkCount'] == 1, f'Hardlinked candidate rejected: {self.path}')
        except BaseException:
            if handle is not None:
                self.k.CloseHandle(handle)
            self.close()
            raise

    def delete(self):
        # FILE_DISPOSITION_INFO uses a one-byte BOOLEAN. Mark only this open file.
        disposition = ctypes.c_ubyte(1)
        if not self.k.SetFileInformationByHandle(self.handle, 4, ctypes.byref(disposition), 1):
            raise ctypes.WinError(ctypes.get_last_error())
        self.close()

    def close(self):
        if self.stream is not None:
            self.stream.close()
            self.stream = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

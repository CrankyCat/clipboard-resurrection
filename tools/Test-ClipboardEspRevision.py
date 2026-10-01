#!/usr/bin/env python3
"""Read-only revision checks; all corruptions are in a temporary fixture."""
import importlib.util
import json
from pathlib import Path
import tempfile
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('esp', Path(__file__).with_name('ClipboardLocalizationEsp.py'))
esp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(esp)
root = esp.ROOT
actual = root / 'package/v240/Clipboard.esp'
assert esp.validate_revision(actual)['record_count'] == 117
checks = 1
with tempfile.TemporaryDirectory(prefix='clipboard-esp-revision-') as temporary:
    fixture = Path(temporary)
    for relative in ('localization/esp-baseline/Clipboard.esp', 'localization/esp-map.json',
                     'localization/esp-revision.json', 'package/base/Clipboard.esp',
                     'package/v240/Clipboard.esp'):
        target = fixture / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((root / relative).read_bytes())
    revision_path = fixture / 'localization/esp-revision.json'
    revision = json.loads(revision_path.read_text())
    target = fixture / 'package/v240/Clipboard.esp'
    with patch.object(esp, 'ROOT', fixture):
        assert esp.validate_revision(target)['record_count'] == 117
        checks += 1
        cases = ('stale_hash', 'extra_header_edit', 'changed_record', 'wrong_deletion')
        for case in cases:
            data = bytearray(actual.read_bytes())
            edited = dict(revision)
            if case == 'extra_header_edit':
                data[16] ^= 1
            elif case == 'changed_record':
                at = data.index(b'EDID')
                data[at + 6] ^= 1
            elif case == 'wrong_deletion':
                edited['removed_form_id'] = '00123456'
            else:
                data[-1] ^= 1
            target.write_bytes(data)
            if case != 'stale_hash':
                # Even updating the revision hash must not permit unrelated edits.
                edited['edited_sha256'] = esp.sha(target)
                (fixture / 'package/base/Clipboard.esp').write_bytes(data)
            revision_path.write_text(json.dumps(edited))
            try:
                esp.validate_revision(target)
            except ValueError:
                checks += 1
            else:
                raise AssertionError(case)
            (fixture / 'package/base/Clipboard.esp').write_bytes(actual.read_bytes())
print(json.dumps({'result': 'passed', 'checks': checks}))

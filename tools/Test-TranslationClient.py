#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline API-request and locale-boundary tests; no real key or API calls."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('translation_client', Path(__file__).with_name('Translate-Localization.py'))
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)


class TranslationClientTests(unittest.TestCase):
    def test_each_locale_has_context_and_docs_follow_explicit_enabled_scope(self):
        sources, _ = client.catalog.load_sources()
        layout = client.catalog.load_document_layout(client.catalog.ROOT)
        for locale in client.catalog.TRANSLATED_LOCALES:
            context, digest = client.translation_context(client.catalog.ROOT, locale)
            self.assertEqual(context['target_locale'], locale)
            self.assertTrue(context['project_context'] and context['name'] and context['style'])
            self.assertEqual(len(digest), 64)
            selected = client.catalog.translation_sources(sources, locale)
            enabled = locale in layout['enabled_locales']
            self.assertEqual(any(row['surface'] == 'docs' for row in selected.values()), enabled)
            self.assertEqual(len(selected), sum(enabled or row['surface'] != 'docs' for row in sources.values()))

    def test_in_game_translation_write_preserves_inactive_document_rows(self):
        key = '$Clipboard_Docs_ReadMe_001'
        archived = {'key': key, 'text': 'Retained fixture wording.', 'source_sha256': '0' * 64}
        current = {'key': '$Clipboard_Test', 'text': 'Fixture.', 'source_sha256': client.catalog.text_hash('Fixture.')}
        source = {'key': current['key'], 'text': 'Fixture.', 'surface': 'papyrus'}
        sources = {source['key']: source}
        layout = {'retained_translation_keys': {'ru': [key]}}
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'ru.json'
            path.write_text(json.dumps([archived, current]), encoding='utf-8')
            existing, _ = client.previous_translations(path, sources)
            retained = client.retained_document_translations(path, sources, layout, 'ru')
            self.assertEqual(retained, {key: archived})
            client.write_translations(path, existing, retained)
            self.assertIn(archived, client.catalog.read_json(path))
            self.assertEqual(client.catalog.load_translations(path, sources, ignored_keys=retained), existing)

    def test_request_preserves_requested_model_endpoint_and_regional_context(self):
        captured = {}
        class Response:
            def __enter__(self): return self
            def __exit__(self, *args): return False
            def read(self, limit): return b'{"choices":[]}'
        class Opener:
            def open(self, request, timeout):
                captured['request'] = request
                return Response()
        context, _ = client.translation_context(client.catalog.ROOT, 'esmx')
        client.request_translation(Opener(), 'offline-sentinel', [], 'gpt-5.6-terra', locale='esmx', context=context)
        request = captured['request']
        self.assertEqual(request.full_url, 'https://api.openai.com/v1/chat/completions')
        payload = json.loads(request.data)
        self.assertEqual(payload['model'], 'gpt-5.6-terra')
        self.assertEqual(json.loads(payload['messages'][1]['content'])['target_locale'], 'esmx')
        self.assertIn('Mexican Spanish', payload['messages'][0]['content'])
        self.assertNotIn('offline-sentinel', request.data.decode())
        self.assertEqual(payload['response_format'], {'type': 'json_object'})

    def test_overlong_mcm_translation_is_rejected_before_acceptance(self):
        row = {'key': '$Clipboard_Test', 'surface': 'mcm', 'text': 'Test', 'context': 'MCM label'}
        response = {'choices': [{'finish_reason': 'stop', 'message': {'content': json.dumps({'translations': [{'key': row['key'], 'text': 'A' * 512}]})}}]}
        with self.assertRaisesRegex(client.catalog.CatalogError, '511 UTF-16'):
            client.parse_response(response, [row])

    def test_endpoint_redirect_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'redirect refused'):
            client.NoRedirect().redirect_request(None, None, 302, '', {}, 'https://example.invalid/')


if __name__ == '__main__':
    unittest.main()

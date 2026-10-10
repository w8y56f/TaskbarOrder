import importlib.util
import json
import pathlib
import unittest

spec = importlib.util.spec_from_file_location('download_symbols', pathlib.Path(__file__).parents[1] / 'scripts/download-symbols.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SymbolReportTests(unittest.TestCase):
    def setUp(self):
        self.report = {
            'format': 1, 'architecture': 'x64',
            'modules': [
                {'module': 'taskbar.dll', 'pdb': 'taskbar.pdb', 'key': '0123456789ABCDEF0123456789ABCDEF1'},
                {'module': 'Taskbar.View.dll', 'pdb': 'Taskbar.View.pdb', 'key': 'FEDCBA9876543210FEDCBA98765432101'},
            ],
        }

    def test_offline_report_roundtrip(self):
        parsed = json.loads(json.dumps(self.report))
        self.assertEqual(module.requests_from_report(parsed), [
            ('taskbar.pdb', '0123456789ABCDEF0123456789ABCDEF1'),
            ('Taskbar.View.pdb', 'FEDCBA9876543210FEDCBA98765432101'),
        ])

    def test_paths_and_unrelated_modules_rejected(self):
        for field, value in [('pdb', '../../escape.pdb'), ('pdb', 'bad/pdb.pdb'),
                             ('pdb', 'C:\\file.pdb'), ('key', '../escape'),
                             ('module', 'unrelated.dll')]:
            with self.subTest(field=field, value=value):
                bad = json.loads(json.dumps(self.report))
                bad['modules'][0][field] = value
                with self.assertRaises(ValueError):
                    module.requests_from_report(bad)

    def test_duplicate_and_partial_reports_rejected(self):
        self.report['modules'][1] = self.report['modules'][0]
        with self.assertRaises(ValueError):
            module.requests_from_report(self.report)
        self.report['modules'] = []
        with self.assertRaises(ValueError):
            module.requests_from_report(self.report)


if __name__ == '__main__':
    unittest.main()

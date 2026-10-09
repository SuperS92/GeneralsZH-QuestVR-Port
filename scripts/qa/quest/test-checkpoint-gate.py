#!/usr/bin/env python3
import copy
import importlib.util
import pathlib
import unittest

path = pathlib.Path(__file__).with_name('compare-checkpoints.py')
spec = importlib.util.spec_from_file_location('gate', path)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class EvidenceGate(unittest.TestCase):
    def setUp(self):
        self.report = {'replaySha256': 'a' * 64, 'simulationHz': 60,
                       'assetsId': 'fixture-assets', 'nameKeysId': 'fixture-key-table',
                       'opened': True, 'completed': True, 'readErrors': 0,
                       'checkpoints': [{'frame': 10, 'expected': 7, 'actual': 7},
                                       {'frame': 20, 'expected': 9, 'actual': 9}]}

    def test_matching_fixture_is_not_live_crossplay(self):
        result = gate.compare(self.report, self.report)
        self.assertEqual(result['checkpointCount'], 2)
        self.assertEqual(result['liveCrossplay'], 'NOT_VERIFIED')

    def test_empty_unreadable_incomplete_and_wrong_profile(self):
        for key, value in (('checkpoints', []), ('opened', False), ('completed', False),
                           ('readErrors', 1), ('simulationHz', 30), ('nameKeysId', '')):
            with self.subTest(key=key):
                bad = copy.deepcopy(self.report); bad[key] = value
                with self.assertRaises(ValueError): gate.compare(self.report, bad)

    def test_first_mismatch_missing_and_duplicate_checkpoint(self):
        for mutation in ('mismatch', 'missing', 'duplicate', 'coverage'):
            bad = copy.deepcopy(self.report)
            if mutation == 'mismatch': bad['checkpoints'][0]['actual'] = 8
            if mutation == 'missing': del bad['checkpoints'][0]['actual']
            if mutation == 'duplicate': bad['checkpoints'][1]['frame'] = 10
            if mutation == 'coverage': bad['checkpoints'].pop()
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                gate.compare(self.report, bad)

    def test_different_assets_and_replay(self):
        for key in ('assetsId', 'replaySha256'):
            bad = copy.deepcopy(self.report); bad[key] = 'b' * 64
            with self.subTest(key=key), self.assertRaises(ValueError):
                gate.compare(self.report, bad)


if __name__ == '__main__': unittest.main()

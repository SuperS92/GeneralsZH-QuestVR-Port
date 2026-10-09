#!/usr/bin/env python3
"""Compare two normalized runtime reports; missing/empty evidence never passes.

Reports contain replaySha256, simulationHz, assetsId, nameKeysId, opened,
completed, readErrors and checkpoints [{frame, expected, actual}]. A matching
fixture establishes only that fixture's checkpoint equality, never live play.
"""
import argparse
import json
import pathlib
import re


def validate(report):
    if report.get('opened') is not True or report.get('completed') is not True:
        raise ValueError('Replay was not opened and completed')
    if report.get('readErrors') != 0:
        raise ValueError('Replay has read errors or missing error count')
    if not re.fullmatch(r'[0-9a-fA-F]{64}', report.get('replaySha256', '')):
        raise ValueError('Missing replay identity')
    for key in ('assetsId', 'nameKeysId'):
        if not isinstance(report.get(key), str) or not report[key].strip():
            raise ValueError('Missing runtime identity: ' + key)
    if report.get('simulationHz') != 60:
        raise ValueError('PC control requires the upstream 60 Hz profile')
    rows = report.get('checkpoints')
    if not isinstance(rows, list) or not rows:
        raise ValueError('Zero/missing checkpoints = NOT_VERIFIED')
    previous = -1
    for row in rows:
        if type(row.get('frame')) is not int or row['frame'] <= previous:
            raise ValueError('Checkpoint frames must be strictly increasing')
        previous = row['frame']
        for key in ('expected', 'actual'):
            if type(row.get(key)) is not int or not 0 <= row[key] <= 0xffffffff:
                raise ValueError('Invalid/missing CRC at frame ' + str(previous))
        if row['expected'] != row['actual']:
            raise ValueError('First PC mismatch at frame ' + str(previous))
    return rows


def compare(control, quest):
    a, b = validate(control), validate(quest)
    for key in ('replaySha256', 'simulationHz', 'assetsId', 'nameKeysId'):
        if control[key] != quest[key]: raise ValueError('Different ' + key)
    if a != b: raise ValueError('Control/Quest checkpoints differ or coverage is incomplete')
    return {'status': 'FIXTURE_MATCH', 'checkpointCount': len(a),
            'lastFrame': a[-1]['frame'], 'liveCrossplay': 'NOT_VERIFIED'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('control', type=pathlib.Path)
    parser.add_argument('quest', type=pathlib.Path)
    args = parser.parse_args()
    try:
        print(json.dumps(compare(json.loads(args.control.read_text()),
                                 json.loads(args.quest.read_text())), indent=2))
    except (ValueError, KeyError, TypeError) as error:
        print(json.dumps({'status': 'NOT_VERIFIED', 'reason': str(error)}))
        raise SystemExit(1)


if __name__ == '__main__': main()

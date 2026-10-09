#!/usr/bin/env python3
"""Compare required native numeric and 60 Hz flags from actual compile commands."""
import argparse
import json
import pathlib

FLAGS = ('-ffp-contract=off', '-fno-strict-aliasing', '-fwrapv',
         '-fno-delete-null-pointer-checks', '-fsanitize=float-cast-overflow',
         '-fsanitize-recover=float-cast-overflow',
         '-DGENERALS_ONLINE_HIGH_FPS_LIMIT=60', '-DGENERALS_ONLINE_HIGH_FPS_FRAME_MULTIPLIER=2')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--control', required=True, type=pathlib.Path)
    parser.add_argument('--quest', required=True, type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    commands = {}
    for name in ('control', 'quest'):
        records = json.loads(getattr(args, name).read_text(encoding='utf-8'))
        matched = [entry['command'] for entry in records if entry['file'].replace('\\', '/')
                   .endswith('/GameLogic/System/GameLogic.cpp')]
        if len(matched) != 1: raise SystemExit('Missing/ambiguous GameLogic compile command')
        commands[name] = matched[0]
    report = {'status': 'PROFILE_FLAGS_PASS', 'flags': {
        flag: {name: flag in command for name, command in commands.items()} for flag in FLAGS},
        'runtimeDeterminism': 'NOT_VERIFIED'}
    if not all(all(entry.values()) for entry in report['flags'].values()):
        raise SystemExit('Required upstream compiler/rate flag missing')
    if '-DSAGE_USE_OPENXR' in commands['control'] or '-DSAGE_USE_OPENXR' not in commands['quest']:
        raise SystemExit('Control/Quest feature isolation failed')
    if args.output: args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__': main()

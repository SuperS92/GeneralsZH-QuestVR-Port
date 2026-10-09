#!/usr/bin/env python3
"""Audit ancestry, explicit upstream edit ledger and fixed offline provenance.

This is a source-boundary check, not a runtime determinism test.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[3]
DONOR = '176a7ce77b615fb8681f8c250625fd3fb5acb4b9'
# Upstream v1.4.0 owns CRC revision selection; Quest uses these exact upstream blobs.
# Pin changes need a source review and fresh replay/live evidence.
REVIEWED_CRC_BLOBS = {'GeneralsMD/Code/GameEngine/Source/GameLogic/System/GameLogic.cpp': 'cd88ace55714e9367584d9cdaef5db8b1baf0af3', 'GeneralsMD/Code/GameEngine/Include/GameLogic/GameLogic.h': '64b5d028efbf05ca56c6e87d1f20ac8124a687f6'}



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--git', default='git')
    parser.add_argument('--offline-source', type=pathlib.Path,
                        default=ROOT / 'work/offline-v044')
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    pin = json.loads((ROOT / 'docs/WORKDIR/quest/UPSTREAM_PIN.json').read_text())
    provenance = json.loads((ROOT / 'docs/WORKDIR/quest/OFFLINE_IMPORTS.json').read_text())
    ledger = json.loads((ROOT / 'docs/WORKDIR/quest/PATCH_LEDGER.json').read_text())
    if pin['offlineDonor'] != DONOR or provenance['donorCommit'] != DONOR:
        raise SystemExit('Offline donor boundary changed')
    git = lambda *argv: subprocess.check_output([args.git, '-C', str(ROOT), *argv], text=True).strip()
    subprocess.run([args.git, '-C', str(ROOT), 'merge-base', '--is-ancestor',
                    pin['commit'], 'HEAD'], check=True)
    files = sorted(set(git('diff', '--name-only', '--ignore-submodules', pin['commit']).splitlines()) |
                   set(git('ls-files', '--others', '--exclude-standard').splitlines()))
    allowed = {row['path'] for row in ledger['upstreamEdits']
               + ledger.get('followUpUpstreamEdits', [])}
    new_prefixes = ('Core/GameEngineDevice/Include/VRDevice/',
                    'GeneralsMD/Code/GameEngineDevice/Source/QuestVR/',
                    'GeneralsMD/Code/Main/QuestVR/', 'docs/WORKDIR/quest/',
                    'scripts/build/quest/', 'scripts/qa/quest/', 'scripts/tooling/quest/')
    extra = {'AGENTS.md', 'CREDITS.md', 'docs/HOWTO/QUEST_SETUP.md',
             'docs/HOWTO/QUEST_CONTROLS.md', '.github/ISSUE_TEMPLATE/feature-request.yaml', 'Patches/quest-v044-dxvk-interop.patch', 'cmake/quest-vr.cmake',
             'Patches/quest-dxvk-d3d8-bridge-exposure.patch',
             'android/app/src/quest/AndroidManifest.xml',
             'android/app/src/quest/res/values/strings.xml',
             'android/app/src/quest/res/values-de/strings.xml',
             'android/app/src/main/java/com/generalsx/zerohour/QuestImportActivity.java',
             'android/app/src/main/java/com/generalsx/zerohour/QuestPlatform.java',
             'docs/DEV_BLOG/2026-09-DIARY.md',
             'docs/DEV_BLOG/2026-10-DIARY.md'}
    unexpected = [p for p in files if p not in allowed | extra and not p.startswith(new_prefixes)]
    if unexpected: raise SystemExit('Unledgered changes: ' + ', '.join(unexpected))
    for row in provenance['imports']:
        source = (args.offline_source / row['source']).resolve()
        if not source.is_relative_to(args.offline_source.resolve()):
            raise SystemExit('Unsafe provenance path')
        actual = hashlib.sha256(source.read_bytes()).hexdigest()
        if actual != row['sourceSha256']: raise SystemExit('Offline source hash mismatch: ' + row['source'])
    # Explicitly protect simulation/network/numeric/profile sources, even if
    # someone expands the ledger in an attempt to conceal a forbidden import.
    protected = ('GeneralsMD/Code/GameEngine/Source/GameNetwork/',
                 'GeneralsMD/Code/GameEngine/Source/GameLogic/',
                 'Core/GameEngine/Source/GameLogic/',
                 'Core/Libraries/Source/WWVegas/WWMath/')
    forbidden = {'GeneralsMD/Code/Main/ReferenceFloatMath.cpp',
                 'cmake/compilers.cmake', 'cmake/config-build.cmake', 'vcpkg.json'}
    for path, expected in REVIEWED_CRC_BLOBS.items():
        if git('hash-object', path) != expected:
            raise SystemExit('Reviewed CRC source changed; fresh review required: ' + path)
    violations = [p for p in files if (p.startswith(protected) and p not in REVIEWED_CRC_BLOBS)
                  or p in forbidden]
    if violations: raise SystemExit('Protected upstream sources modified: ' + ', '.join(violations))
    report = {'status': 'SOURCE_BOUNDARY_PASS', 'upstream': pin['commit'],
              'donor': DONOR, 'verifiedImportEntries': len(provenance['imports']),
              'ledgeredUpstreamFiles': len(allowed),
              'reviewedCRCBlobs': REVIEWED_CRC_BLOBS,
              'runtimeDeterminism': 'NOT_VERIFIED'}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__': main()

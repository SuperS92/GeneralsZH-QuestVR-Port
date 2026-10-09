#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise production campaign selection and XR frame handoff without game assets."""
import argparse, json, pathlib, subprocess
root=pathlib.Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tools', required=True, type=pathlib.Path)
args=parser.parse_args()
tools=json.loads(args.tools.read_text())
work=root/'work/campaign-test'; work.mkdir(exist_ok=True)
exe=work/'test.exe'
subprocess.run([str(pathlib.Path(tools['hostCxxRuntime'])/'clang++.exe'), '-std=c++17', '-O2', '-static',
    '-I'+str(root/'Core/GameEngineDevice/Include'),str(root/'scripts/qa/quest/test-campaign.cpp'),
    '-o',str(exe)],check=True)
result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
(work/'result.log').write_text(result.stdout+result.stderr)
print(result.stdout+result.stderr)
if result.returncode: raise SystemExit(result.returncode)
(root/'work/evidence/campaign-host.json').write_text(json.dumps(dict(status='HOST_CAMPAIGN_PASS',
    productionModel=True, limits='Does not exercise real OpenXR, D3D targets, game scripts or headset image/controls.'),indent=2))

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check actual conservative culling against independent triangle clipping."""
import argparse,json,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tools',required=True,type=pathlib.Path)
args=parser.parse_args(); tools=json.loads(args.tools.read_text())
work=root/'work/terrain-culling-smoke'; work.mkdir(parents=True,exist_ok=True)
exe=work/'terrain-culling.exe'
subprocess.run([str(pathlib.Path(tools['hostCxxRuntime'])/'clang++.exe'),'-std=c++17','-O2','-static',
    '-D__ANDROID__','-DSAGE_USE_OPENXR','-I'+str(root/'Core/GameEngineDevice/Include'),
    str(root/'scripts/qa/quest/test-terrain-culling.cpp'),
    str(root/'GeneralsMD/Code/Main/QuestVR/QuestTerrainCulling.cpp'),'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=120)
log=result.stdout+result.stderr; (work/'result.log').write_text(log,encoding='utf-8'); print(log)
if result.returncode: raise SystemExit(result.returncode)
(root/'work/evidence/terrain-culling-host.json').write_text(json.dumps({
    'status':'HOST_CONSERVATIVE_CULL_PASS','actualProductionHelper':True,
    'checks':'Full cell coverage and duplicate/partial writes; interior peak and expanding bounds; invalid data/planes/pass; float rounding guard; asymmetric rotated separate-eye groups; independent actual triangle clipping; nested scoped native suppression',
    'nativeRendererPixels':'NOT_EXERCISED','questFPSAndCrossplay':'NOT_VERIFIED'},indent=2)+'\n',encoding='utf-8')

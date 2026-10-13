#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise actual fog helpers on the host D3D9 GPU through a D3D8 subset adapter."""
import argparse,json,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tools',required=True,type=pathlib.Path)
parser.add_argument('--cpu-only',action='store_true',help='Run CPU upload/footprint/index oracles; keep GPU validation explicitly pending')
args=parser.parse_args();tools=json.loads(args.tools.read_text())
work=root/'work/shroud-pass-smoke';work.mkdir(parents=True,exist_ok=True)
exe=work/'shroud-pass.exe'
compiler=pathlib.Path(tools['hostCxxRuntime'])
definition=work/'d3d9.def'
definition.write_text('LIBRARY d3d9.dll\nEXPORTS\nDirect3DCreate9\n',encoding='utf-8')
subprocess.run([str(compiler/'llvm-dlltool.exe'),'-m','i386:x86-64','-d',str(definition),'-l',str(work/'libd3d9.a')],check=True)
subprocess.run([str(pathlib.Path(tools['hostCxxRuntime'])/'clang++.exe'),'-std=c++17','-O2','-static',
    '-D__ANDROID__','-DSAGE_USE_OPENXR','-I'+str(root/'scripts/qa/quest/shroud-d3d9-bridge'),'-I'+str(root/'Core/GameEngineDevice/Include'),
    '-I'+str(root/'scripts/qa/quest/host-shims'),str(root/'GeneralsMD/Code/Main/QuestVR/QuestPerformance.cpp'),
    str(root/'GeneralsMD/Code/Main/QuestVR/QuestShroudPass.cpp'),str(root/'scripts/qa/quest/test-shroud-pass.cpp'),
    str(work/'libd3d9.a'),'-luser32','-o',str(exe)],check=True)
result=subprocess.run([str(exe)]+(['--cpu-only'] if args.cpu_only else []),capture_output=True,text=True,timeout=120)
log=result.stdout+result.stderr;(work/'result.log').write_text(log,encoding='utf-8');print(log)
if result.returncode:raise SystemExit(f'Host fixture failed: {result.returncode:#x}; see result.log')
(root/'work/evidence/shroud-pass-host.json').write_text(json.dumps({
    'status':'HOST_CPU_ORACLE_PASS_GPU_PENDING' if args.cpu_only else 'HOST_D3D9_BRIDGE_PIXEL_PASS',
    'actualProductionHelper':True,'gpuCases':0 if args.cpu_only else 24,
    'backend':'CPU only; D3D9 fixture unavailable in current restricted host' if args.cpu_only else 'Real D3D9 GPU with host-only D3D8 contract adapter; not Quest DXVK',
    'checks':'Unknown/failed/partial upload destination oracle; actual float sampling footprint; 2046 retained index masks'
        +('' if args.cpu_only else '; real GPU exact RGBA, point/linear, eye transforms, fades, RGB mask/alpha, copy failure, resource reset, state fallback and unchanged state'),
    'questDXVKPerformanceAndCrossplay':'NOT_VERIFIED'},indent=2)+'\n',encoding='utf-8')

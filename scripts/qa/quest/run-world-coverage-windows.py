#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the production D3D8 coverage code against a stateful raster/COM fixture."""
import argparse,json,pathlib,re,subprocess,os
root=pathlib.Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tools',required=True,type=pathlib.Path)
tools=json.loads(p.parse_args().tools.read_text());compiler=pathlib.Path(tools['hostCxxRuntime'])
work=root/'work/world-coverage-smoke';work.mkdir(exist_ok=True)
header=(compiler.parent/'include/d3d8.h').read_text()
generated=['// Generated test-only defaults from the installed SDK. Never linked into the app.','#pragma once','#include <d3d8.h>']
for interface in ['IDirect3DDevice8','IDirect3DSurface8','IDirect3DTexture8']:
    body=header.split('DECLARE_INTERFACE_IID_('+interface+',',1)[1].split('};',1)[0]
    generated.append('struct Stub'+interface[1:]+' : '+interface+' {')
    matches=list(re.finditer(r'STDMETHOD(?:_\(([^,]+),\s*(\w+)\)|\((\w+)\))\s*\(THIS_?\s*(.*?)\)\s*PURE;',body,re.S))
    assert matches
    for m in matches:
        typ=(m[1] or 'HRESULT').strip();name=m[2] or m[3];args=m[4].strip()
        value='' if typ=='void' else 'return E_NOTIMPL;' if typ=='HRESULT' else 'return {};'
        generated.append(typ+' STDMETHODCALLTYPE '+name+'('+args+') override {'+value+'}')
    generated.append('};')
(work/'coverage-stubs.h').write_text('\n'.join(generated)+'\n')
exe=work/'coverage-smoke.exe';env=dict(os.environ,TMP=str(work),TEMP=str(work))
source=root/'GeneralsMD/Code/Main/QuestVR/QuestWorldCoverage.cpp'
subprocess.run([str(compiler/'clang++.exe'),'-std=c++17','-O1','-static','-Wno-microsoft-exception-spec','-D__ANDROID__','-DSAGE_USE_OPENXR',
    '-I'+str(work),'-I'+str(root/'scripts/qa/quest/host-shims'),'-I'+str(root/'Core/GameEngineDevice/Include'),
    str(source),str(root/'scripts/qa/quest/test-world-coverage.cpp'),'-o',str(exe)],check=True,env=env)
r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
(work/'result.log').write_text(r.stdout+r.stderr);print(r.stdout)
if r.returncode:print(r.stderr)
assert r.returncode==0
report=dict(status='PRODUCTION_WORLD_COVERAGE_HOST_PASS',cases=int(re.search(r'PASS (\d+) coverage cases',r.stdout)[1]),
    scope='Production D3D8 code; stateful software raster/COM fixture, not Quest GPU evidence',
    headset='NOT_TESTED',gpuCost='NOT_MEASURED')
(root/'work/evidence/world-coverage-host.json').write_text(json.dumps(report,indent=2)+'\n')

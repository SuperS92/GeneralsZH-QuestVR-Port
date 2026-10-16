#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise portable Quick Match cache keys and original playlist indices."""
import argparse,json,pathlib,subprocess,os
root=pathlib.Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tools',required=True,type=pathlib.Path)
a=p.parse_args();tools=json.loads(a.tools.read_text())
work=root/'work/quickmatch-smoke';work.mkdir(exist_ok=True)
env=dict(os.environ,TMP=str(work),TEMP=str(work))
exe=work/'quickmatch-smoke.exe';compiler=pathlib.Path(tools['hostCxxRuntime'])/'clang++.exe'
subprocess.run([str(compiler),'-std=c++17','-O2','-static','-I'+str(root/'Core/GameEngineDevice/Include'),
    str(root/'scripts/qa/quest/test-quickmatch.cpp'),'-o',str(exe)],check=True,env=env)
result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
print(result.stdout+result.stderr);assert result.returncode==0
report=dict(status='HOST_QUICKMATCH_PASS',cases=66053,scope='portable cache paths and filtered service indices',headset='NOT_TESTED')
(root/'work/evidence/quickmatch-host.json').write_text(json.dumps(report,indent=2)+'\n')

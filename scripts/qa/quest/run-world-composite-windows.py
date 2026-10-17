#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the production two-eye R8/colour image handoff on the host GPU."""
from pathlib import Path
import argparse,json,re,subprocess,os
root=Path(__file__).resolve().parents[3];p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tools',required=True,type=Path)
tools=json.loads(p.parse_args().tools.read_text());c=Path(tools['hostCxxRuntime']);work=root/'work/world-composite-smoke';work.mkdir(exist_ok=True)
source=root/'GeneralsMD/Code/Main/QuestVR/QuestWorldComposite.cpp';test=root/'scripts/qa/quest/test-world-composite.cpp'
functions=sorted(set(re.findall(r'\b(vk[A-Z]\w*)\s*\(',source.read_text()+test.read_text())))
definition=work/'vulkan.def';definition.write_text('LIBRARY vulkan-1.dll\nEXPORTS\n'+'\n'.join(functions)+'\n')
subprocess.run([str(c/'llvm-dlltool.exe'),'-m','i386:x86-64','-d',str(definition),'-l',str(work/'libvulkan.a')],check=True)
exe=work/'world-composite.exe';env=dict(os.environ,TMP=str(work),TEMP=str(work))
subprocess.run([str(c/'clang++.exe'),'-std=c++17','-O1','-static','-D__ANDROID__','-DSAGE_USE_OPENXR',
 '-I'+str(root/'references/fbraz3-dxvk/include/vulkan/include'),'-I'+str(source.parent),str(source),str(test),str(work/'libvulkan.a'),'-o',str(exe)],check=True,env=env)
result=subprocess.run([str(exe),str(work)],capture_output=True,text=True,timeout=60)
(work/'result.log').write_text(result.stdout+result.stderr);print(result.stdout+result.stderr);assert result.returncode==0
report=dict(status='HOST_GPU_WORLD_COMPOSITE_PASS',cases=int(re.search(r'PASS (\d+) actual GPU world composite cases',result.stdout)[1]),
 scope='Production Vulkan WorldComposite class, actual host GPU; not complete DXVK game, XR runtime or Quest driver',
 colors='Byte-exact RGBA/BGRA',alpha='Same-eye retained R8 mask, original material alpha ignored only at handoff',
 headset='NOT_TESTED',questPerformance='NOT_MEASURED')
(root/'work/evidence/world-composite-host.json').write_text(json.dumps(report,indent=2)+'\n')

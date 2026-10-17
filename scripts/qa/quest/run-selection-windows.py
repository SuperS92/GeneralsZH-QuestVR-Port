#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise extracted production VR selection functions with UI/message test doubles."""
import argparse,json,os,pathlib,re,subprocess
root=pathlib.Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tools',required=True,type=pathlib.Path)
tools=json.loads(p.parse_args().tools.read_text());work=root/'work/selection-smoke';work.mkdir(exist_ok=True)
source=(root/'GeneralsMD/Code/GameEngineDevice/Source/QuestVR/QuestVRInput.cpp').read_text()
def block(name,struct=False):
    at=source.index(('struct ' if struct else '')+name+('' if struct else '('));start=source.rfind('\n',0,at)+1
    opening=source.index('{',at);depth=1;end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]+(';' if struct else '')
parts=[block('BoxSelectionState',True)+'\nBoxSelectionState g_boxSelection;']
parts += [block(n) for n in ['additiveSelectionHeld','canExtendUnitSelection','selectionLimit','selectUnderRay']]
parts += [block('BoxSelectionContext',True)]
parts += [block(n) for n in ['addDrawableToBoxSelection','selectBox','updateBoxSelection','smartTriggerUnderRay']]
(work/'selection-production.inc').write_text('\n'.join(parts)+'\n')
compiler=pathlib.Path(tools['hostCxxRuntime'])/'clang++.exe';exe=work/'selection.exe'
subprocess.run([str(compiler),'-std=c++17','-O1','-static','-D__ANDROID__','-DSAGE_USE_OPENXR',
 '-I'+str(work),'-I'+str(root/'Core/GameEngineDevice/Include'),str(root/'scripts/qa/quest/test-selection.cpp'),'-o',str(exe)],
 check=True,env=dict(os.environ,TMP=str(work),TEMP=str(work)))
result=subprocess.run([str(exe)],text=True,capture_output=True,timeout=30);print(result.stdout+result.stderr);assert result.returncode==0
assert source.index('if (tabletopGrabbed) {',source.index('extern "C" void QuestVRInput_Update'))<source.index('const Bool additive = additiveSelectionHeld')
assert 'const Bool additiveClick = g_boxSelection.armed ? g_boxSelection.additive : additive;' in source
assert source.index('if (rightController.menuPointValid != 0)',source.index('extern "C" void QuestVRInput_Update'))<source.index('const Bool additive = additiveSelectionHeld')
report=dict(status='PRODUCTION_SELECTION_HOST_PASS',cases=int(re.search(r'PASS (\d+)',result.stdout)[1]),
 scope='Extracted production select/click/box/gesture functions; UI and standard group-message model, not complete engine dispatch or network',
 binding='left grip + right trigger; both grips reserved',headset='NOT_TESTED',liveCrossplay='NOT_TESTED')
(root/'work/evidence/selection-host.json').write_text(json.dumps(report,indent=2)+'\n')

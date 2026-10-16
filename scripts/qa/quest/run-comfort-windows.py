#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check rigid comfort transforms and menu/tilt gesture ownership on the host."""
import argparse,json,pathlib,subprocess,os
root=pathlib.Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tools',required=True,type=pathlib.Path)
a=p.parse_args();tools=json.loads(a.tools.read_text())
work=root/'work/comfort-smoke';work.mkdir(exist_ok=True)
env=dict(os.environ,TMP=str(work),TEMP=str(work))
compiler=pathlib.Path(tools['hostCxxRuntime'])/'clang++.exe';exe=work/'comfort-smoke.exe'
subprocess.run([str(compiler),'-std=c++17','-O2','-ffp-contract=off','-static',
    '-I'+str(root/'Core/GameEngineDevice/Include'),str(root/'scripts/qa/quest/test-comfort.cpp'),'-o',str(exe)],check=True,env=env)
result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
print(result.stdout+result.stderr);assert result.returncode==0
native=(root/'GeneralsMD/Code/GameEngineDevice/Source/QuestVR/QuestVRInput.cpp').read_text()
presentation=(root/'GeneralsMD/Code/GameEngineDevice/Source/QuestVR/QuestVRPresentation.cpp').read_text()
bootstrap=(root/'GeneralsMD/Code/Main/QuestVR/QuestOpenXRBootstrap.cpp').read_text()
ui=(root/'GeneralsMD/Code/GameEngineDevice/Source/SDL3GameEngine.cpp').read_text()
assert 'g_comfortDegrees = QuestComfort::ClampDegrees(preferences.getReal("TabletopTiltDegrees", 0.0f))' in native
assert 'g_comfortSpaceGeneration = generation' in native and 'QuestComfort::Point' in native
assert 'QuestComfort::Orientation' in presentation and 'QuestComfort::Point' in presentation
assert 'change->session == m_session && change->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL' in bootstrap
assert '*it <= m_predictedDisplayTime' in bootstrap
assert 'const int wheel = menuScroll.Step(controller.stick[1], injectIntoLegacyUi && flatMenu' in ui
report=dict(status='HOST_COMFORT_PASS',transformCases=9020,gestureChecks='PASS',headsetQuality='NOT_TESTED',
    caveat='Pure geometry/input and source-contract checks; not full engine runtime, headset comfort or crossplay certification')
(root/'work/evidence/comfort-host.json').write_text(json.dumps(report,indent=2)+'\n')

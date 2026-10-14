#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check actual completed-eye scope ordering/gates; native GPU store ops remain unverified."""
import argparse,json,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--tools',required=True,type=pathlib.Path)
args=parser.parse_args();tools=json.loads(args.tools.read_text())
work=root/'work/depth-discard-smoke';work.mkdir(exist_ok=True)
compiler=pathlib.Path(tools['hostCxxRuntime'])/'clang++.exe';exe=work/'depth-discard-smoke.exe'
subprocess.run([str(compiler),'-std=c++17','-static','-D__ANDROID__','-DSAGE_USE_OPENXR',
    '-I'+str(root/'Core/GameEngineDevice/Include'),'-I'+str(root/'scripts/qa/quest/host-shims'),
    str(root/'GeneralsMD/Code/Main/QuestVR/QuestDepthDiscard.cpp'),str(root/'scripts/qa/quest/test-depth-discard.cpp'),
    '-o',str(exe)],check=True)
result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
(work/'result.log').write_text(result.stdout+result.stderr);print(result.stdout+result.stderr)
if result.returncode:raise SystemExit(result.returncode)
ww=(root/'GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp').read_text()
end=ww.split('WW3DErrorType WW3D::End_Render(bool flip_frame)')[1].split('return WW3D_ERROR_OK;')[0]
assert end.index('SortingRendererClass::Flush();')<end.index('DX8Wrapper::End_Scene(flip_frame);')
wrapper=(root/'Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp').read_text().split('void DX8Wrapper::End_Scene(bool flip_frames)')[1]
assert wrapper.index('Pillarbox_End();')<wrapper.index('DiscardCompletedEyeDepth')<wrapper.index('DX8CALL(EndScene());')
presentation=(root/'GeneralsMD/Code/GameEngineDevice/Source/QuestVR/QuestVRPresentation.cpp').read_text()
assert presentation.count('EyeDepthDiscardScope completedEye')==1
report={'status':'HOST_COMPLETED_EYE_SCOPE_PASS','actualHelper':True,'scopeIterations':200,
        'nativeDxvkD3D9Contract':'NATIVE_BUILD_AND_HEADSET_PENDING','gpuStoreOpEffect':'NOT_MEASURED',
        'imageLifecycleCrossplay':'NOT_VERIFIED'}
(root/'work/evidence/depth-discard-host.json').write_text(json.dumps(report,indent=2)+'\n')

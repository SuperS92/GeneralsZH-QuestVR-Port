#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check conservative runtime mask geometry and actual depth/stencil GPU effects."""
import argparse,json,pathlib,re,shutil,subprocess
root=pathlib.Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--tools',required=True,type=pathlib.Path)
p.add_argument('--glslang',required=True,type=pathlib.Path)
a=p.parse_args();tools=json.loads(a.tools.read_text())
work=root/'work/visibility-mask-smoke';work.mkdir(exist_ok=True)
sysroot=pathlib.Path(tools['ndk'])/'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include'
for name in ('vulkan','vk_video'):shutil.copytree(sysroot/name,work/'include'/name,dirs_exist_ok=True)
frag=work/'solid.frag';frag.write_text('#version 450\nlayout(location=0) out vec4 color;\nvoid main(){color=vec4(1,0,0,1);}\n')
spirv=work/'solid.spv'
subprocess.run([str(a.glslang),'-V','--target-env','vulkan1.0','-o',str(spirv),str(frag)],check=True)
test=root/'scripts/qa/quest/test-visibility-mask.cpp'
functions=sorted(set(re.findall(r'\b(vk[A-Z]\w*)\s*\(',test.read_text())))
definition=work/'vulkan.def';definition.write_text('LIBRARY vulkan-1.dll\nEXPORTS\n'+'\n'.join(functions)+'\n')
compiler=pathlib.Path(tools['hostCxxRuntime'])
subprocess.run([str(compiler/'llvm-dlltool.exe'),'-m','i386:x86-64','-d',str(definition),'-l',str(work/'libvulkan.a')],check=True)
exe=work/'visibility-mask-smoke.exe'
subprocess.run([str(compiler/'clang++.exe'),'-O2','-std=c++17','-static','-I'+str(work/'include'),
    '-I'+str(root/'Core/GameEngineDevice/Include'),'-I'+str(root/'GeneralsMD/Code/Main/QuestVR'),
    str(test),str(work/'libvulkan.a'),'-o',str(exe)],check=True)
result=subprocess.run([str(exe),str(spirv)],capture_output=True,text=True,timeout=60)
log=result.stdout+result.stderr;(work/'result.log').write_text(log,encoding='utf-8');print(log)
if result.returncode:raise SystemExit(result.returncode)
source=(root/'GeneralsMD/Code/Main/QuestVR/QuestOpenXRBootstrap.cpp').read_text()
body=source.split('bool Bootstrap::applyVisibilityMask')[1].split('bool Bootstrap::captureTaaCamera')[0]
assert body.count('device->Clear(')==1 and 'D3DCLEAR_ZBUFFER,0,0.0f,0' in body
assert all(name not in body for name in ('D3DCLEAR_STENCIL','D3DCLEAR_TARGET','SetTransform','SetRenderState','DrawIndexedPrimitive','SetViewport','FlushRenderingCommands'))
assert 'color==m_eyeSurfaces[eye]&&depth==m_depthSurface' in body
assert 'MaskProjectionMatches(matrix,fov)' in body
assert 'XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR' in body
assert 'mask.vertexCountOutput<=vertices.size()&&mask.indexCountOutput<=indices.size()' in body
assert 'QuestXR_TaaRequested()&&QuestXR_TaaMode()!=3' in body
presentation=(root/'GeneralsMD/Code/GameEngineDevice/Source/QuestVR/QuestVRPresentation.cpp').read_text()
segment=presentation[presentation.index('void W3DDisplay::drawQuestXRStereo'):]
assert segment.index('WW3D::Begin_Render(true, true,')<segment.index('QuestXR_ApplyVisibilityMask')<segment.index('WW3D::Render(m_3DScene')
assert segment.index('DX8Wrapper::Apply_Render_State_Changes();')<segment.index('QuestXR_ApplyVisibilityMask')
report=dict(status='HOST_VISIBILITY_MASK_PASS',gpuCases=int(re.search(r'PASS (\d+) GPU visibility cases',log)[1]),
    checks=['Independent guarded-pixel containment and malformed input/work cap','Asymmetric projection and wrong-eye/vertical rejection',
        'Two odd extents, two eye meshes, mask Off/On','Actual depth-tested GPU output: exact visible color/alpha, every stencil value preserved',
        'Native source gates: owned target/depth, exact viewport/projection, hidden mesh only, bounded runtime buffers',
        'No object visibility/update changes; depth-only clear after camera binding before world'],
    limits='Host fixture is not full Vulkan validation or proof of runtime mask availability, renderer-wide early-depth efficiency, headset quality/performance or crossplay')
(root/'work/evidence/visibility-mask-host.json').write_text(json.dumps(report,indent=2)+'\n')

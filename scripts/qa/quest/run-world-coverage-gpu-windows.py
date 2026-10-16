#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute DXVK's production coverage emission and independent attachment blending on the host GPU."""
from pathlib import Path
import argparse,json,re,subprocess,os
r=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tools',required=True,type=Path)
tools=json.loads(p.parse_args().tools.read_text());c=Path(tools['hostCxxRuntime'])
work=r/'work/world-coverage-gpu';work.mkdir(exist_ok=True);dxvk=r/'references/fbraz3-dxvk'
env=dict(os.environ,TMP=str(work),TEMP=str(work))
assert 'EmitQuestCoverage' in (dxvk/'src/d3d9/d3d9_spec_constants.h').read_text(), 'Configure the versioned Quest backend patches first'
shader=r/'scripts/qa/quest/emit-world-coverage-shader.cpp';test=r/'scripts/qa/quest/test-world-coverage-gpu.cpp'
subprocess.run([str(c/'clang++.exe'),'-std=c++17','-O1','-static','-ffunction-sections','-Wl,--gc-sections','-DSAGE_USE_OPENXR',
 '-I'+str(dxvk/'src'),'-I'+str(dxvk/'include/spirv/include'),'-I'+str(dxvk/'include/vulkan/include'),str(shader),
 str(dxvk/'src/spirv/spirv_module.cpp'),str(dxvk/'src/spirv/spirv_code_buffer.cpp'),'-o',str(work/'emit.exe')],check=True,env=env)
subprocess.run([str(work/'emit.exe'),str(work)],check=True)
functions=sorted(set(re.findall(r'\b(vk[A-Z]\w*)\s*\(',test.read_text())))
definition=work/'vulkan.def';definition.write_text('LIBRARY vulkan-1.dll\nEXPORTS\n'+'\n'.join(functions)+'\n')
subprocess.run([str(c/'llvm-dlltool.exe'),'-m','i386:x86-64','-d',str(definition),'-l',str(work/'libvulkan.a')],check=True)
subprocess.run([str(c/'clang++.exe'),'-std=c++17','-O1','-static','-I'+str(dxvk/'include/vulkan/include'),
 str(test),str(work/'libvulkan.a'),'-o',str(work/'coverage-gpu.exe')],check=True,env=env)
vertex=work/'coverage.vert';vertex.write_text('#version 450\nvoid main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);gl_Position=vec4(p*2.-1.,0.,1.);}\n')
glslang=next((Path(folder)/'glslang.exe' for folder in tools['path'] if (Path(folder)/'glslang.exe').is_file()),None)
assert glslang, 'Configure glslang in the local tool profile'
subprocess.run([str(glslang),'-V',str(vertex),'-o',str(work/'coverage.vert.spv')],check=True)
result=subprocess.run([str(work/'coverage-gpu.exe'),str(work)],capture_output=True,text=True,timeout=60)
(work/'result.log').write_text(result.stdout+result.stderr);print(result.stdout+result.stderr);assert result.returncode==0
report=dict(status='HOST_GPU_WORLD_COVERAGE_PASS',cases=int(re.search(r'PASS (\d+) actual GPU cases',result.stdout)[1]),
 scope='Production DXVK SPIR-V emitter and independent MRT blend; not the complete D3D8/DXVK game or Quest driver',
 quickAndOptimizedSpecialization=True,originalMaterialRgba='PASS',headset='NOT_TESTED',questGpuCost='NOT_MEASURED')
(r/'work/evidence/world-coverage-gpu.json').write_text(json.dumps(report,indent=2)+'\n')

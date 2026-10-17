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
patch=(r/'Patches/quest-dxvk-r8-world-coverage.patch').read_text()
logicalMask=re.findall(r'^\+\s*mode\.writeMask=(VK_COLOR_COMPONENT_[A-Z]_BIT);',patch,re.M)
assert len(logicalMask)==1,'Bind regression to the versioned backend write mask'
# Compile the production pure functions without linking unrelated device/logging code.
def function(text, name):
    begin=text.index(name+'(')
    begin=text.rfind('\n',0,begin)+1
    opening=text.index('{',begin); depth=1; end=opening+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[begin:end]
util=(dxvk/'src/dxvk/dxvk_util.cpp').read_text()
functionsText='\n'.join(function(util,n) for n in ['remapComponentFlag','remapComponentMask','findComponentSwizzle','invertComponentMapping','remapAlphaToColorBlendFactor','getComponentIndex'])
formats=(dxvk/'src/d3d9/d3d9_format.cpp').read_text()
a8Case=formats.split('case D3D9Format::A8: return {',1)[1].split('};',1)[0]
production=work/'production-a8.cpp'
production.write_text('#include "d3d9/d3d9_format.h"\n#include "dxvk/dxvk_util.h"\nnamespace dxvk::util {\n'+functionsText+'\n}\nnamespace dxvk { D3D9_VK_FORMAT_MAPPING ProductionA8Mapping(){return {'+a8Case+'};}}\n')
functions=sorted(set(re.findall(r'\b(vk[A-Z]\w*)\s*\(',test.read_text())))
definition=work/'vulkan.def';definition.write_text('LIBRARY vulkan-1.dll\nEXPORTS\n'+'\n'.join(functions)+'\n')
subprocess.run([str(c/'llvm-dlltool.exe'),'-m','i386:x86-64','-d',str(definition),'-l',str(work/'libvulkan.a')],check=True)
subprocess.run([str(c/'clang++.exe'),'-std=c++17','-O1','-static','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
 '-DVK_USE_PLATFORM_WIN32_KHR','-include','windows.h','-Wno-nontrivial-memcall',
 '-DQUEST_COVERAGE_LOGICAL_WRITE_MASK='+logicalMask[0],'-I'+str(dxvk/'src'),
 '-I'+str(dxvk/'include/spirv/include'),'-I'+str(dxvk/'include/vulkan/include'),
 str(test),str(production),str(work/'libvulkan.a'),'-o',str(work/'coverage-gpu.exe')],check=True,env=env)
vertex=work/'coverage.vert';vertex.write_text('#version 450\nvoid main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);gl_Position=vec4(p*2.-1.,0.,1.);}\n')
glslang=next((Path(folder)/'glslang.exe' for folder in tools['path'] if (Path(folder)/'glslang.exe').is_file()),None)
assert glslang, 'Configure glslang in the local tool profile'
subprocess.run([str(glslang),'-V',str(vertex),'-o',str(work/'coverage.vert.spv')],check=True)
result=subprocess.run([str(work/'coverage-gpu.exe'),str(work)],capture_output=True,text=True,timeout=60)
(work/'result.log').write_text(result.stdout+result.stderr);print(result.stdout+result.stderr);assert result.returncode==0
report=dict(status='HOST_GPU_WORLD_COVERAGE_PASS',cases=int(re.search(r'PASS (\d+) actual GPU cases',result.stdout)[1]),
 scope='Production DXVK A8 format/write-mask conversion, SPIR-V emitter and independent MRT blend; not complete game or Quest driver',
 logicalWriteMask=logicalMask[0],productionA8Mapping=True,quickAndOptimizedSpecialization=True,originalMaterialRgba='PASS',headset='NOT_TESTED',questGpuCost='NOT_MEASURED')
(r/'work/evidence/world-coverage-gpu.json').write_text(json.dumps(report,indent=2)+'\n')

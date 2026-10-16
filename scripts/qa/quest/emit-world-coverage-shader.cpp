// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <cstdint>
#include <fstream>
#include "dxvk/dxvk_limits.h"
#include "d3d9/d3d9_spec_constants.h"
int main(int argc,char**argv){
 using namespace dxvk; if(argc!=2)return 1;
 for(unsigned test=0;test<5;++test){
 SpirvModule m(0x00010300); m.enableCapability(spv::CapabilityShader);m.setMemoryModel(spv::AddressingModelLogical,spv::MemoryModelGLSL450);
 const auto f=m.defFloatType(32),v=m.defVectorType(f,4),u=m.defIntType(32,0);
 const uint32_t members[6]={u,u,u,u,u,u};const auto block=m.defStructType(6,members);
 m.decorateBlock(block);for(unsigned i=0;i<6;++i)m.memberDecorateOffset(block,i,i*4);
 const auto ubo=m.newVar(m.defPointerType(block,spv::StorageClassUniform),spv::StorageClassUniform);
 m.decorateDescriptorSet(ubo,0);m.decorateBinding(ubo,0);
 const auto out0=m.newVar(m.defPointerType(v,spv::StorageClassOutput),spv::StorageClassOutput);
 const auto out3=m.newVar(m.defPointerType(v,spv::StorageClassOutput),spv::StorageClassOutput);
 m.decorateLocation(out0,0);m.decorateLocation(out3,3);
 const auto mainId=m.allocateId();m.setExecutionMode(mainId,spv::ExecutionModeOriginUpperLeft);
 m.functionBegin(m.defVoidType(),mainId,m.defFunctionType(m.defVoidType(),0,nullptr),spv::FunctionControlMaskNone);m.opLabel(m.allocateId());
 const auto color=m.constvec4f32(.2f,.4f,.6f,float(test)/4);m.opStore(out0,color);
 D3D9ShaderSpecConstantManager spec;EmitQuestCoverage(m,spec,ubo,color,out3);
 m.opReturn();m.functionEnd();m.addEntryPoint(mainId,spv::ExecutionModelFragment,"main");
 auto code=m.compile();std::ofstream file(std::string(argv[1])+"/coverage-"+std::to_string(test)+".frag.spv",std::ios::binary);file.write(reinterpret_cast<const char*>(code.data()),code.size());
 }
}

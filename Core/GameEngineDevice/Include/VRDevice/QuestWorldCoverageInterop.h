// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <d3d8.h>
namespace QuestXR {
// Exact complete inherited public + Quest-depth vtable, then the new owned IID.
struct WorldCoverageInterop : IUnknown {
 virtual void STDMETHODCALLTYPE GetVulkanHandles(void*,void*,void*)=0;
 virtual void STDMETHODCALLTYPE GetSubmissionQueue(void*,void*,void*)=0;
 virtual void STDMETHODCALLTYPE TransitionTextureLayout(void*,const void*,int,int)=0;
 virtual void STDMETHODCALLTYPE FlushRenderingCommands()=0;
 virtual void STDMETHODCALLTYPE LockSubmissionQueue()=0;
 virtual void STDMETHODCALLTYPE ReleaseSubmissionQueue()=0;
 virtual void STDMETHODCALLTYPE LockDevice()=0;
 virtual void STDMETHODCALLTYPE UnlockDevice()=0;
 virtual bool STDMETHODCALLTYPE WaitForResource(void*,DWORD)=0;
 virtual HRESULT STDMETHODCALLTYPE CreateImage(const void*,void**)=0;
 virtual HRESULT STDMETHODCALLTYPE DiscardCompletedEyeDepth(void*)=0;
 virtual HRESULT STDMETHODCALLTYPE BeginWorldCoverage(void*,void*)=0;
 virtual HRESULT STDMETHODCALLTYPE EndWorldCoverage()=0;
};
}
#endif

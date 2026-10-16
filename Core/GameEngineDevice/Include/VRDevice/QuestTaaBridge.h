// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include "VRDevice/QuestTaaMath.h"
struct IDirect3DDevice8;
extern "C" unsigned QuestXR_TaaMode();
extern "C" bool QuestXR_TaaRequested();
extern "C" bool QuestXR_CaptureTaaCamera(IDirect3DDevice8 *, unsigned, QuestXR::TaaCamera *);
extern "C" bool QuestXR_ApplyTaa(IDirect3DDevice8 *, unsigned, const QuestXR::TaaCamera *);
#endif

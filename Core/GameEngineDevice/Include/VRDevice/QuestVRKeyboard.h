// SPDX-License-Identifier: GPL-3.0-or-later
// GeneralsX @feature Codex 09/10/2026 Modal text entry on the existing Quest UI quad.
#pragma once
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include <string>

struct QuestXRControllerState;
namespace QuestVRKeyboard {
using Result = void (*)(const std::u16string &, int serial, bool done, bool submit);
bool Show(const std::u16string &text, int maxLength, int flags, int serial, Result result);
void Hide();
bool IsOpen();
bool InputBlocked();
void UpdatePointer(const QuestXRControllerState &controller, bool valid);
void Draw();
void Shutdown();
}
#endif

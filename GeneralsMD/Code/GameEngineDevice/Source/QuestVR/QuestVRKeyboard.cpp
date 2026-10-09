// SPDX-License-Identifier: GPL-3.0-or-later
// GeneralsX @feature Codex 09/10/2026 In-quad keyboard; existing Android editor owns filtering/focus.
#include "VRDevice/QuestVRKeyboard.h"
#if defined(__ANDROID__) && defined(SAGE_USE_OPENXR)
#include "VRDevice/QuestVRKeyboardModel.h"
#include "VRDevice/QuestXRInterface.h"
#include "Common/GameCommon.h"
#include "Common/Override.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include <vector>

namespace QuestVRKeyboard {
namespace {
Model model;
bool active = false, blocked = false;
int currentSerial = 0;
Result callback = nullptr;
std::vector<DisplayString *> labels;

void Finish(bool submit) {
    active = false;
    const Result result = callback;
    callback = nullptr;
    if (result) result(model.Text(), currentSerial, true, submit);
}
void Label(size_t slot, const std::u16string &value, int x, int y, int width, int height,
    UnsignedInt color) {
    if (!TheDisplayStringManager || !TheFontLibrary) return;
    if (labels.size() <= slot) labels.resize(slot + 1, nullptr);
    if (!labels[slot]) labels[slot] = TheDisplayStringManager->newDisplayString();
    if (!labels[slot]) return;
    UnicodeString text;
    for (char16_t unit : value) text.concat(static_cast<WideChar>(unit));
    DisplayString *label = labels[slot];
    label->setText(text);
    // GeneralsX @bugfix Codex 09/10/2026 FontLibrary rejects sizes above 100;
    // a 2208-pixel UI otherwise silently keeps the tiny default font on keys.
    int fontSize = MIN(100, MAX(18, height * 2 / 3));
    int textWidth = 0, textHeight = 0;
    do {
        GameFont *font = TheFontLibrary->getFont("Arial", fontSize, TRUE);
        if (!font) {
            if (fontSize <= 14) return;
            fontSize -= 2;
            continue;
        }
        label->setFont(font);
        label->getSize(&textWidth, &textHeight);
        if ((textWidth <= width - 8 && textHeight <= height - 8) || fontSize <= 14) break;
        fontSize -= 2;
    } while (true);
    label->draw(x + MAX(0, (width - textWidth) / 2), y + MAX(0, (height - textHeight) / 2),
        color, GameMakeColor(0, 0, 0, 220));
}
}

bool Show(const std::u16string &text, int maxLength, int flags, int serial, Result result) {
    if (!result) return false;
    model.Open(text, maxLength, flags);
    currentSerial = serial;
    callback = result;
    active = true;
    return true;
}
void Hide() { active = false; callback = nullptr; }
bool IsOpen() { return active; }
bool InputBlocked() { return active || blocked; }
void UpdatePointer(const QuestXRControllerState &controller, bool valid) {
    blocked = active;
    if (!active) return;
    if (controller.pressed & QUEST_XR_BUTTON_SECONDARY) { Finish(false); return; }
    const int key = model.Pointer(valid, controller.menuPoint[0], controller.menuPoint[1],
        (controller.pressed & QUEST_XR_BUTTON_TRIGGER) != 0,
        (controller.released & QUEST_XR_BUTTON_TRIGGER) != 0);
    if (key < 0) return;
    const Action action = model.Keys()[key].action;
    if (action == Action::Done || action == Action::Close) {
        Finish(action == Action::Done); return;
    }
    if (model.Activate(key) && callback) callback(model.Text(), currentSerial, false, false);
}
void Draw() {
    if (!active || !TheDisplay) return;
    const int width = TheDisplay->getWidth(), height = TheDisplay->getHeight();
    if (width <= 0 || height <= 0) return;
    const UnsignedInt text = GameMakeColor(240, 245, 250, 255);
    TheDisplay->drawFillRect(0, 0, width, height, GameMakeColor(7, 14, 24, 245));
    TheDisplay->drawFillRect(width * 7 / 100, height * 15 / 100, width * 86 / 100,
        height * 9 / 100, GameMakeColor(25, 42, 60, 255));
    Label(0, u"Enter text", width / 10, height * 4 / 100, width * 8 / 10, height * 7 / 100, text);
    Label(1, model.DisplayText(), width * 8 / 100, height * 15 / 100, width * 84 / 100,
        height * 9 / 100, text);
    for (size_t i = 0; i < model.Keys().size(); ++i) {
        const Key &key = model.Keys()[i];
        const int x = static_cast<int>(key.x * width), y = static_cast<int>(key.y * height);
        const int w = static_cast<int>(key.width * width), h = static_cast<int>(key.height * height);
        const bool hover = static_cast<int>(i) == model.Hover();
        const bool held = static_cast<int>(i) == model.Held();
        TheDisplay->drawFillRect(x, y, w, h, held ? GameMakeColor(40, 140, 170, 255)
            : hover ? GameMakeColor(45, 95, 125, 255) : GameMakeColor(28, 48, 68, 255));
        Label(i + 3, key.label, x, y, w, h, text);
    }
    Label(2, u"Right trigger: select    B: close", width / 10, height * 87 / 100,
        width * 8 / 10, height * 5 / 100, GameMakeColor(165, 185, 205, 255));
}
void Shutdown() {
    Hide(); blocked = false;
    if (TheDisplayStringManager)
        for (DisplayString *label : labels)
            if (label) TheDisplayStringManager->freeDisplayString(label);
    labels.clear();
}
}
#endif

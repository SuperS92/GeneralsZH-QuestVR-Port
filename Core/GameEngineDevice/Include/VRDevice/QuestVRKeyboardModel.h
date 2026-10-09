// SPDX-License-Identifier: GPL-3.0-or-later
// GeneralsX @feature Codex 09/10/2026 Portable editor and release-to-select keyboard layout.
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace QuestVRKeyboard {
enum class Action { Character, Backspace, Clear, Shift, Symbols, Space, Left, Right, Done, Close };
struct Key {
    std::u16string label;
    Action action;
    char16_t character = 0;
    float x, y, width, height;
    bool Contains(float px, float py) const {
        return std::isfinite(px) && std::isfinite(py) && px >= x && py >= y
            && px < x + width && py < y + height;
    }
};

class Model {
public:
    void Open(const std::u16string &initial, int maxLength, int flags) {
        text.clear();
        maximum = maxLength > 0 ? static_cast<size_t>(maxLength) : 256;
        secret = (flags & 1) != 0;
        digitsOnly = (flags & 2) != 0;
        for (char16_t ch : initial)
            if (ch >= 0x20 && !(ch >= 0xD800 && ch <= 0xDFFF) && text.size() < maximum)
                text.push_back(ch);
        cursor = text.size();
        shift = symbols = false;
        hover = held = -1;
        BuildKeys();
    }
    const std::u16string &Text() const { return text; }
    std::u16string DisplayText() const {
        std::u16string visible = secret ? std::u16string(text.size(), u'*') : text;
        visible.insert(cursor, 1, u'|');
        const size_t start = cursor > 36 ? cursor - 36 : 0;
        return visible.substr(start, 48);
    }
    const std::vector<Key> &Keys() const { return keys; }
    int Hover() const { return hover; }
    int Held() const { return held; }
    int Hit(float x, float y) const {
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i].Contains(x, y)) return static_cast<int>(i);
        return -1;
    }
    // Release must land on the same key as the press. Lost tracking cancels a press.
    int Pointer(bool valid, float x, float y, bool pressed, bool released) {
        hover = valid ? Hit(x, y) : -1;
        if (!valid) { held = -1; return -1; }
        if (pressed) held = hover;
        if (!released) return -1;
        const int chosen = held == hover ? held : -1;
        held = -1;
        return chosen;
    }
    bool Activate(int index) {
        if (index < 0 || static_cast<size_t>(index) >= keys.size()) return false;
        const Key key = keys[index];
        switch (key.action) {
            case Action::Character: return Insert(key.character);
            case Action::Space: return Insert(u' ');
            case Action::Backspace:
                if (!cursor) return false;
                text.erase(--cursor, 1); return true;
            case Action::Clear:
                if (text.empty()) return false;
                text.clear(); cursor = 0; return true;
            case Action::Left: if (cursor) --cursor; break;
            case Action::Right: if (cursor < text.size()) ++cursor; break;
            case Action::Shift: shift = !shift; BuildKeys(); break;
            case Action::Symbols: symbols = !symbols; BuildKeys(); break;
            case Action::Done: case Action::Close: break;
        }
        return false;
    }
private:
    bool Insert(char16_t ch) {
        if (text.size() >= maximum || ch < 0x20 || (ch >= 0xD800 && ch <= 0xDFFF)
            || (digitsOnly && (ch < u'0' || ch > u'9'))) return false;
        text.insert(cursor++, 1, ch);
        return true;
    }
    void Characters(const std::u16string &row, int number) {
        const float cell = 0.86f / static_cast<float>(row.size());
        for (size_t i = 0; i < row.size(); ++i) {
            char16_t ch = row[i];
            if (shift && ch >= u'a' && ch <= u'z') ch -= u'a' - u'A';
            if (shift && ch == u'\u00e4') ch = u'\u00c4';
            if (shift && ch == u'\u00f6') ch = u'\u00d6';
            if (shift && ch == u'\u00fc') ch = u'\u00dc';
            keys.push_back({std::u16string(1, ch), Action::Character, ch,
                0.07f + cell * i, 0.29f + number * 0.079f, cell - 0.006f, 0.07f});
        }
    }
    void Button(const char16_t *label, Action action, float x, float y, float width) {
        keys.push_back({label, action, 0, x, y, width, 0.07f});
    }
    void BuildKeys() {
        keys.clear();
        Characters(u"1234567890", 0);
        if (!digitsOnly) {
            Characters(symbols ? u"!@#$%^&*()" : u"qwertyuiop", 1);
            Characters(symbols ? u"-_+=[]{}<>" : u"asdfghjkl-", 2);
            Characters(symbols ? u".,:;?/\\|~`" : u"zxcvbnm.,?", 3);
            Characters(u"\u00e4\u00f6\u00fc\u00df", 4);
            Button(shift ? u"SHIFT" : u"Shift", Action::Shift, 0.07f, 0.685f, 0.16f);
            Button(symbols ? u"ABC" : u"#+=", Action::Symbols, 0.24f, 0.685f, 0.16f);
        }
        Button(u"<", Action::Left, 0.41f, 0.685f, 0.10f);
        Button(u">", Action::Right, 0.52f, 0.685f, 0.10f);
        Button(u"Backspace", Action::Backspace, 0.63f, 0.685f, 0.30f);
        if (!digitsOnly) Button(u"Space", Action::Space, 0.07f, 0.764f, 0.32f);
        Button(u"Clear", Action::Clear, 0.40f, 0.764f, 0.16f);
        Button(u"Close", Action::Close, 0.57f, 0.764f, 0.16f);
        Button(u"Done", Action::Done, 0.74f, 0.764f, 0.19f);
    }
    std::u16string text;
    size_t cursor = 0, maximum = 256;
    bool shift = false, symbols = false, secret = false, digitsOnly = false;
    int hover = -1, held = -1;
    std::vector<Key> keys;
};
}

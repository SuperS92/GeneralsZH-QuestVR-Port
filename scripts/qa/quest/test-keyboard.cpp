// SPDX-License-Identifier: GPL-3.0-or-later
// GeneralsX @test Codex 09/10/2026 User input sequences against the production editor model.
#include "VRDevice/QuestVRKeyboardModel.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace QuestVRKeyboard;
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
int find(Model &model, Action action, char16_t character = 0) {
    for (size_t i = 0; i < model.Keys().size(); ++i)
        if (model.Keys()[i].action == action && (action != Action::Character || model.Keys()[i].character == character))
            return static_cast<int>(i);
    throw std::runtime_error("Missing key");
}
void type(Model &model, char16_t character) { model.Activate(find(model, Action::Character, character)); }
int main() {
    Model m;
    m.Open(u"Match", 12, 0);
    m.Activate(find(m, Action::Space));
    type(m, u'2');
    require(m.Text() == u"Match 2", "Room name append");
    m.Activate(find(m, Action::Left));
    m.Activate(find(m, Action::Backspace));
    require(m.Text() == u"Match2", "Backspace at caret");
    m.Activate(find(m, Action::Space));
    require(m.Text() == u"Match 2", "Insert at caret");
    m.Activate(find(m, Action::Clear));
    m.Activate(find(m, Action::Shift));
    type(m,u'Q'); type(m,u'\u00c4');
    require(m.Text() == u"Q\u00c4", "Shift and BMP characters");
    m.Activate(find(m,Action::Symbols)); type(m,u'@');
    require(m.Text() == u"Q\u00c4@", "Symbol layout");

    m.Open(u"", 3, 0); type(m,u'a'); type(m,u'b'); type(m,u'c'); type(m,u'd');
    require(m.Text() == u"abc", "Maximum length");
    m.Activate(find(m,Action::Backspace)); type(m,u'd');
    require(m.Text() == u"abd", "Delete frees length slot");
    m.Open(u"12", 3, 2);
    require(find(m,Action::Character,u'3') >= 0, "Digits exist");
    for (const Key &k : m.Keys()) if (k.action == Action::Character)
        require(k.character >= u'0' && k.character <= u'9', "Numeric-only keyboard");
    type(m,u'3'); type(m,u'4'); require(m.Text() == u"123", "Numeric limit");
    m.Open(u"Secret", 20, 1);
    require(m.DisplayText().find(u"Secret") == std::u16string::npos, "Password masked");
    require(m.DisplayText() == u"******|", "Password display");
    m.Open(std::u16string({u'A',u'\n',0xD800,u'B'}),20,0);
    require(m.Text() == u"AB", "Controls and surrogate input excluded");

    for (int flags : {0,1,2}) {
        m.Open(u"",20,flags);
        for (size_t i = 0; i < m.Keys().size(); ++i) {
            const Key k = m.Keys()[i];
            const float x = k.x + k.width/2, y = k.y + k.height/2;
            require(k.x >= 0 && k.y >= 0 && k.x+k.width <= 1 && k.y+k.height <= 1, "Keys in visible quad");
            require(m.Hit(x,y) == static_cast<int>(i), "Rendered geometry matches hit test");
            require(m.Pointer(true,x,y,true,false) == -1, "No character on press");
            require(m.Pointer(true,x,y,false,true) == static_cast<int>(i), "Same-key release");
        }
    }
    const Key first=m.Keys()[0], second=m.Keys()[1];
    const float x=first.x+first.width/2,y=first.y+first.height/2;
    m.Pointer(true,x,y,true,false);
    require(m.Pointer(true,second.x+second.width/2,y,false,true)==-1,"Release on another key cancels");
    m.Pointer(true,x,y,true,false); m.Pointer(false,x,y,false,false);
    require(m.Pointer(true,x,y,false,true)==-1,"Tracking loss cancels held key");
    require(m.Hit(std::numeric_limits<float>::quiet_NaN(),y)==-1,"Reject NaN pointer");
    require(m.Hit(-1,y)==-1 && m.Hit(2,y)==-1,"Outside panel");
    m.Pointer(true,x,y,true,false); m.Open(u"new field",20,0);
    require(m.Pointer(true,x,y,false,true)==-1,"Retarget cancels old press");
    std::cout << "PASS: room name editing, caret, length/digits/password/BMP filters, layouts, ray release and retarget\n";
}

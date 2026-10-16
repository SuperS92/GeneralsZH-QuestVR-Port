// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestQuickMatch.h"
#include <cassert>
#include <cstdio>
#include <vector>

int main() {
    // A mixed absolute Android prefix and Windows suffix must match POSIX map cache keys.
    assert(QuestQuickMatch::PortablePath("/data/app/files/UserData/maps\\[RANK] Test\\[RANK] Test.map")
        == "/data/app/files/UserData/maps/[RANK] Test/[RANK] Test.map");
    assert(QuestQuickMatch::PortablePath("maps\\Tournament Desert\\Tournament Desert.map")
        == "maps/Tournament Desert/Tournament Desert.map");
    assert(QuestQuickMatch::PortablePath("maps/雪/雪.map") == "maps/雪/雪.map");
    assert(QuestQuickMatch::PortablePath("").empty());
    unsigned cases=4;
    // Exhaust every available/selected combination. Missing rows must never renumber service maps.
    for (unsigned available=0;available<256;++available) {
        std::vector<int> rows;
        for (int original=0;original<8;++original) if (available&(1u<<original)) rows.push_back(original);
        for (unsigned selected=0;selected<256;++selected) {
            std::vector<int> actual,expected;
            for (int row=0;row<static_cast<int>(rows.size());++row) if (selected&(1u<<row)) {
                int index=-1;assert(QuestQuickMatch::PlaylistIndex(rows,row,index));actual.push_back(index);
            }
            unsigned row=0;
            for(int original=0;original<8;++original) if (available&(1u<<original)) {
                if(selected&(1u<<row)) expected.push_back(original);
                ++row;
            }
            assert(actual==expected);++cases;
        }
        int sentinel=73;
        assert(!QuestQuickMatch::PlaylistIndex(rows,-1,sentinel) && sentinel==73);
        assert(!QuestQuickMatch::PlaylistIndex(rows,static_cast<int>(rows.size()),sentinel) && sentinel==73);
        cases+=2;
    }
    int sentinel=73;assert(!QuestQuickMatch::PlaylistIndex({-1},0,sentinel) && sentinel==73);++cases;
    std::printf("PASS %u Quick Match path/filtered-selection cases\n",cases);
}

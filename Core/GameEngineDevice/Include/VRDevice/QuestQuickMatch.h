// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace QuestQuickMatch {
// GeneralsX @bugfix Codex 16/10/2026 Match POSIX cache keys without changing service map identities.
inline std::string PortablePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

// Filtered list rows are not indices in the service's complete playlist.
inline bool PlaylistIndex(const std::vector<int> &rows, int row, int &index) {
    if (row < 0 || static_cast<size_t>(row) >= rows.size() || rows[row] < 0) return false;
    index = rows[row];
    return true;
}
}

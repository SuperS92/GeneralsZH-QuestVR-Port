// SPDX-License-Identifier: GPL-3.0-or-later
package com.generalsx.zerohour;

import java.io.File;
import java.io.IOException;

/** Quest launcher default, using the upstream engine's existing marker contract. */
final class QuestPcCompatibility {
    static void ensureDefault(boolean quest, boolean enabled, File privateGameFolder) throws IOException {
        if (!quest || !enabled || privateGameFolder == null || !privateGameFolder.isDirectory()) return;
        File marker = new File(privateGameFolder, "gx_pc_compat.txt");
        if (!marker.isFile() && !marker.createNewFile()) {
            throw new IOException("PC compatibility marker could not be created");
        }
    }
    private QuestPcCompatibility() {}
}

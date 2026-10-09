// SPDX-License-Identifier: GPL-3.0-or-later
package com.generalsx.zerohour;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
public final class QuestPcCompatibilityTest {
    public static void main(String[] args) throws Exception {
        Path root = Files.createTempDirectory(Path.of(args[0]), "pc-defaults-");
        File folder = Files.createDirectory(root.resolve("quest")).toFile();
        Path marker = folder.toPath().resolve("gx_pc_compat.txt");
        QuestPcCompatibility.ensureDefault(false, true, folder);
        if (Files.exists(marker)) throw new AssertionError("Ordinary Android folder changed");
        QuestPcCompatibility.ensureDefault(true, false, folder);
        if (Files.exists(marker)) throw new AssertionError("Explicit Off ignored");
        QuestPcCompatibility.ensureDefault(true, true, folder);
        if (!Files.isRegularFile(marker)) throw new AssertionError("Fresh default did not create marker");
        byte[] sentinel = {1,2,3}; Files.write(marker, sentinel);
        QuestPcCompatibility.ensureDefault(true, true, folder);
        if (!Arrays.equals(sentinel, Files.readAllBytes(marker))) throw new AssertionError("Existing marker overwritten");
        Files.delete(marker);
        QuestPcCompatibility.ensureDefault(true, false, folder);
        if (Files.exists(marker)) throw new AssertionError("User Off did not survive a later launch");
        QuestPcCompatibility.ensureDefault(true, true, root.resolve("missing").toFile());
        if (Files.exists(root.resolve("missing"))) throw new AssertionError("Missing import folder created");
        QuestPcCompatibility.ensureDefault(true, true, null);
        Files.createDirectory(marker);
        boolean failed = false;
        try { QuestPcCompatibility.ensureDefault(true, true, folder); }
        catch (java.io.IOException expected) { failed = true; }
        if (!failed) throw new AssertionError("Invalid marker was silently accepted");
        Files.delete(marker); Files.delete(folder.toPath()); Files.delete(root);
        System.out.println("PASS fresh Quest marker, preserved contents, deliberate Off, ordinary Android exclusion and invalid import/marker handling");
    }
}

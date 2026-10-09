// SPDX-License-Identifier: GPL-3.0-or-later
package com.generalsx.zerohour;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.util.AtomicFile;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import java.util.Locale;
import java.util.UUID;

/** Copy legally owned data through SAF into a new private directory. */
public final class QuestImportActivity extends Activity {
    private TextView status;
    private Button rootButton, baseButton;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(24, 24, 24, 24);
        status = new TextView(this);
        status.setText("Import your own Zero Hour files. Select the ZH root; a ZH_Generals child supplies the base game. You can also import the base game separately. Originals are only read.");
        panel.addView(status);
        rootButton = button(panel, "Import Zero Hour folder", 1);
        baseButton = button(panel, "Import base Generals folder", 2);
        setContentView(panel);
    }
    private Button button(LinearLayout panel, String text, int request) {
        Button result = new Button(this);
        result.setText(text);
        result.setOnClickListener(v -> {
            Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            pick.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivityForResult(pick, request);
        });
        panel.addView(result);
        return result;
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        Uri tree = data.getData();
        rootButton.setEnabled(false); baseButton.setEnabled(false);
        status.setText("Copying and validating files. Keep this window open.");
        new Thread(() -> {
            File destination = new File(getFilesDir(), "Imported/" + UUID.randomUUID());
            try {
                copyTree(tree, DocumentsContract.getTreeDocumentId(tree), destination, 0);
                File base = request == 2 ? destination : child(destination, "ZH_Generals");
                if (request == 1) requireArchive(destination, "INIZH.big");
                if (base != null) validateBase(base);
                validateArchives(destination);
                String key = request == 1 ? SetupActivity.PREF_GAME_PATH : SetupActivity.PREF_BASE_GENERALS_PATH;
                android.content.SharedPreferences prefs = getSharedPreferences(SetupActivity.PREFS_NAME, MODE_PRIVATE);
                String oldRoot = prefs.getString(SetupActivity.PREF_GAME_PATH, null);
                String oldBase = prefs.getString(SetupActivity.PREF_BASE_GENERALS_PATH, null);
                byte[] oldRootMarker = readMarker("gamedata_path.txt");
                byte[] oldBaseMarker = readMarker("generals_base_path.txt");
                try {
                    marker(request == 1 ? "gamedata_path.txt" : "generals_base_path.txt", destination);
                    android.content.SharedPreferences.Editor edit = prefs.edit().putString(key, destination.getAbsolutePath());
                    if (request == 1 && base != null) {
                        marker("generals_base_path.txt", base);
                        edit.putString(SetupActivity.PREF_BASE_GENERALS_PATH, base.getAbsolutePath());
                    }
                    if (!edit.commit()) throw new IOException("Could not save import settings");
                } catch (Exception selectionError) {
                    restoreMarker("gamedata_path.txt", oldRootMarker);
                    restoreMarker("generals_base_path.txt", oldBaseMarker);
                    if (!prefs.edit().putString(SetupActivity.PREF_GAME_PATH, oldRoot)
                        .putString(SetupActivity.PREF_BASE_GENERALS_PATH, oldBase).commit())
                        throw new IOException("Import selection failed; previous settings could not be restored", selectionError);
                    throw selectionError;
                }
                runOnUiThread(() -> {
                    status.setText("Import validated. Return to Setup to launch. Base-game artwork is required as well as the ZH expansion.");
                    rootButton.setEnabled(true); baseButton.setEnabled(true);
                });
            } catch (Exception error) {
                // Keep a failed, unselected import for diagnosis; never delete player files.
                runOnUiThread(() -> {
                    status.setText("Import was not selected: " + error.getMessage());
                    rootButton.setEnabled(true); baseButton.setEnabled(true);
                });
            }
        }, "QuestDataImport").start();
    }
    private void copyTree(Uri tree, String document, File output, int depth) throws IOException {
        if (depth > 16) throw new IOException("Folder nesting exceeds 16 levels");
        if (!output.mkdirs() && !output.isDirectory()) throw new IOException("Cannot create import folder");
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, document);
        String[] fields = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE};
        HashSet<String> names = new HashSet<>();
        try (Cursor cursor = getContentResolver().query(children, fields, null, null, null)) {
            if (cursor == null) throw new IOException("Folder provider did not return its files");
            while (cursor.moveToNext()) {
                String id = cursor.getString(0), name = cursor.getString(1), mime = cursor.getString(2);
                if (name == null || name.isEmpty() || name.equals(".") || name.equals("..")
                    || name.contains("/") || name.contains("\\") || !names.add(name.toLowerCase(Locale.ROOT)))
                    throw new IOException("Unsafe or duplicate file name");
                File target = new File(output, name);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    copyTree(tree, id, target, depth + 1);
                } else {
                    Uri file = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                    try (InputStream in = getContentResolver().openInputStream(file);
                         FileOutputStream out = new FileOutputStream(target)) {
                        if (in == null) throw new IOException("Provider could not open " + name);
                        byte[] buffer = new byte[1024 * 1024];
                        for (int size; (size = in.read(buffer)) != -1;) out.write(buffer, 0, size);
                        out.getFD().sync();
                    }
                }
            }
        }
    }
    private static File child(File parent, String wanted) {
        File[] entries = parent.listFiles();
        if (entries != null) for (File file : entries) if (file.getName().equalsIgnoreCase(wanted)) return file;
        return null;
    }
    private static void requireArchive(File parent, String name) throws IOException {
        File file = child(parent, name);
        if (file == null || !file.isFile()) throw new IOException("Missing " + name);
    }
    private static void validateBase(File root) throws IOException {
        for (String name : new String[]{"INI.big", "Terrain.big", "Textures.big", "W3D.big"}) requireArchive(root, name);
    }
    private static void validateArchives(File root) throws IOException {
        File[] entries = root.listFiles();
        if (entries == null) throw new IOException("Cannot read imported folder");
        for (File file : entries) {
            if (file.isDirectory()) validateArchives(file);
            else if (file.getName().toLowerCase(Locale.ROOT).endsWith(".big")) {
                try (RandomAccessFile archive = new RandomAccessFile(file, "r")) {
                    if (archive.length() < 16) throw new IOException("Empty/truncated " + file.getName());
                    int magic = archive.readInt();
                    if (magic != 0x42494746 && magic != 0x42494734) throw new IOException("Invalid BIG header: " + file.getName());
                    archive.readInt(); // BIGF file-size field is historically endian-dependent.
                    long count = Integer.toUnsignedLong(archive.readInt());
                    long tableEnd = Integer.toUnsignedLong(archive.readInt());
                    if (count > 1000000 || tableEnd < 16 || tableEnd > archive.length()) throw new IOException("Invalid BIG table");
                    for (long i = 0; i < count; i++) {
                        if (archive.getFilePointer() + 9 > tableEnd) throw new IOException("Truncated BIG table");
                        long offset = Integer.toUnsignedLong(archive.readInt());
                        long size = Integer.toUnsignedLong(archive.readInt());
                        // Stock Steam PatchZH.big contains a zero-length Data\\* marker
                        // at offset zero. It has no payload to overlap the table.
                        if (!(offset == 0 && size == 0)
                            && (offset < tableEnd || offset + size > archive.length()))
                            throw new IOException("Invalid BIG entry in " + file.getName() + " (#" + i + ")");
                        int character;
                        do {
                            if (archive.getFilePointer() >= tableEnd) throw new IOException("Unterminated BIG name");
                            character = archive.readUnsignedByte();
                        } while (character != 0);
                    }
                }
            }
        }
    }
    private void marker(String name, File folder) throws IOException {
        restoreMarker(name, (folder.getAbsolutePath() + "\n").getBytes(StandardCharsets.UTF_8));
    }
    private byte[] readMarker(String name) throws IOException {
        AtomicFile target = new AtomicFile(new File(getFilesDir(), name));
        if (!target.getBaseFile().exists()) return null;
        return target.readFully();
    }
    private void restoreMarker(String name, byte[] content) throws IOException {
        AtomicFile target = new AtomicFile(new File(getFilesDir(), name));
        if (content == null) { target.delete(); return; }
        FileOutputStream stream = target.startWrite();
        try {
            stream.write(content);
            target.finishWrite(stream);
        } catch (IOException error) { target.failWrite(stream); throw error; }
    }
}

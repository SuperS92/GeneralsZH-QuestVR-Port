// SPDX-License-Identifier: GPL-3.0-or-later
package com.generalsx.zerohour;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.HashSet;
import java.util.Set;

/** GeneralsX @feature Codex 10/10/2026 Full signed Quest APK updates, separate from online data. */
final class QuestApkUpdater {
    static final class Release {
        final String version, name, url, sha256;
        final long size;
        Release(String version, String name, String url, String digest, long size) {
            QuestReleasePolicy.validateAsset(version, name, url, digest, size);
            this.version = version; this.name = name; this.url = url;
            this.sha256 = digest.substring(7).toLowerCase(java.util.Locale.ROOT); this.size = size;
        }
    }
    interface Progress { void update(long bytes, long total); }

    static Release check() throws Exception {
        HttpURLConnection connection = (HttpURLConnection) new URL(QuestReleasePolicy.API).openConnection();
        connection.setConnectTimeout(15000); connection.setReadTimeout(20000);
        connection.setRequestProperty("Accept", "application/vnd.github+json");
        connection.setRequestProperty("User-Agent", "GeneralsZH-QuestVR-Updater");
        connection.setInstanceFollowRedirects(false);
        try {
            int status = connection.getResponseCode();
            if (status == 404) return null; // no published stable release yet
            if (status != 200) throw new java.io.IOException("GitHub HTTP " + status);
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            try (InputStream input = connection.getInputStream()) {
                byte[] buffer = new byte[8192]; int count;
                while ((count = input.read(buffer)) != -1) {
                    if (bytes.size() + count > 1024 * 1024) throw new java.io.IOException("Release metadata too large");
                    bytes.write(buffer, 0, count);
                }
            }
            JSONObject release = new JSONObject(new String(bytes.toByteArray(), StandardCharsets.UTF_8));
            if (release.getBoolean("draft") || release.getBoolean("prerelease"))
                throw new java.io.IOException("Release is not public and stable");
            String tag = release.getString("tag_name");
            if (!tag.startsWith("v")) throw new java.io.IOException("Invalid release tag");
            String version = tag.substring(1), name = "GeneralsZH-QuestVR-" + version + ".apk";
            JSONArray assets = release.getJSONArray("assets");
            Release found = null;
            for (int i = 0; i < assets.length(); ++i) {
                JSONObject asset = assets.getJSONObject(i);
                if (!name.equals(asset.getString("name"))) continue;
                if (found != null || !"uploaded".equals(asset.getString("state")))
                    throw new java.io.IOException("Quest APK is not uniquely available");
                found = new Release(version, name, asset.getString("browser_download_url"),
                    asset.getString("digest"), asset.getLong("size"));
            }
            if (found == null) throw new java.io.IOException("Release has no verified Quest APK");
            return found;
        } finally { connection.disconnect(); }
    }

    static synchronized File download(Context context, Release release, Progress progress) throws Exception {
        File directory = new File(context.getCacheDir(), "quest-apk-update");
        if (!directory.isDirectory() && !directory.mkdirs()) throw new java.io.IOException("Cannot create update cache");
        File complete = new File(directory, "update.apk"), partial = new File(directory, "update-part.apk");
        if (complete.isFile()) {
            try { verify(context, complete, release); return complete; }
            catch (Exception stale) {
                if (!complete.delete()) throw new java.io.IOException("Cannot replace cached update");
            }
        }
        if (directory.getUsableSpace() < release.size + 32L * 1024 * 1024)
            throw new java.io.IOException("Not enough free space for APK download");
        HttpURLConnection connection = openDownload(release.url);
        try (InputStream input = connection.getInputStream(); FileOutputStream output = new FileOutputStream(partial)) {
            byte[] buffer = new byte[128 * 1024]; long total = 0, lastReport = 0; int count;
            while ((count = input.read(buffer)) != -1) {
                total += count;
                if (total > release.size) throw new java.io.IOException("APK exceeds expected size");
                output.write(buffer, 0, count);
                long now = System.currentTimeMillis();
                if (now - lastReport >= 250) { progress.update(total, release.size); lastReport = now; }
            }
            output.getFD().sync();
            if (total != release.size) throw new java.io.IOException("Incomplete APK download");
        } catch (Exception failure) {
            partial.delete(); throw failure;
        } finally { connection.disconnect(); }
        try {
            verify(context, partial, release);
            if (!partial.renameTo(complete)) throw new java.io.IOException("Cannot finalize APK download");
        } catch (Exception failure) { partial.delete(); throw failure; }
        progress.update(release.size, release.size);
        return complete;
    }

    private static HttpURLConnection openDownload(String address) throws Exception {
        for (int redirects = 0; redirects <= 5; ++redirects) {
            if (!QuestReleasePolicy.allowedDownloadUrl(address)) throw new java.io.IOException("Untrusted APK download URL");
            HttpURLConnection connection = (HttpURLConnection) new URL(address).openConnection();
            connection.setInstanceFollowRedirects(false);
            connection.setConnectTimeout(15000); connection.setReadTimeout(60000);
            connection.setRequestProperty("User-Agent", "GeneralsZH-QuestVR-Updater");
            try {
                int status = connection.getResponseCode();
                if (status == 200) return connection;
                String location = connection.getHeaderField("Location");
                if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
                    && location != null) {
                    address = new URL(new URL(address), location).toExternalForm();
                    connection.disconnect(); continue;
                }
                throw new java.io.IOException("APK download HTTP " + status);
            } catch (Exception failure) { connection.disconnect(); throw failure; }
        }
        throw new java.io.IOException("Too many APK redirects");
    }

    static void verify(Context context, File file, Release release) throws Exception {
        if (!BuildConfig.QUEST_VR) throw new SecurityException("Quest updater is unavailable in the phone build");
        if (file.length() != release.size) throw new java.io.IOException("APK size mismatch");
        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        try (InputStream input = new FileInputStream(file)) {
            byte[] buffer = new byte[128 * 1024]; int count;
            while ((count = input.read(buffer)) != -1) digest.update(buffer, 0, count);
        }
        StringBuilder hash = new StringBuilder();
        for (byte b : digest.digest()) hash.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
        if (!release.sha256.equals(hash.toString())) throw new SecurityException("APK checksum mismatch");
        PackageManager manager = context.getPackageManager();
        PackageInfo installed = manager.getPackageInfo(context.getPackageName(), PackageManager.GET_SIGNING_CERTIFICATES);
        PackageInfo archive = manager.getPackageArchiveInfo(file.getAbsolutePath(), PackageManager.GET_SIGNING_CERTIFICATES);
        if (archive == null || !context.getPackageName().equals(archive.packageName)
            || !release.version.equals(archive.versionName)
            || archive.getLongVersionCode() <= installed.getLongVersionCode()
            || QuestReleasePolicy.compareVersions(archive.versionName, installed.versionName) <= 0)
            throw new SecurityException("APK is not a newer version of this Quest app");
        if (installed.signingInfo == null || archive.signingInfo == null
            || !signers(installed).equals(signers(archive)))
            throw new SecurityException("APK signing key differs from the installed app");
    }

    private static Set<String> signers(PackageInfo info) {
        Set<String> result = new HashSet<>();
        for (Signature signature : info.signingInfo.getApkContentsSigners()) result.add(signature.toCharsString());
        if (result.isEmpty()) throw new SecurityException("Missing APK signing certificate");
        return result;
    }
}

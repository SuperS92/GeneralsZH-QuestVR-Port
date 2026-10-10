// SPDX-License-Identifier: GPL-3.0-or-later
package com.generalsx.zerohour;

import java.net.URI;

/** GeneralsX @feature Codex 10/10/2026 Pure policy for the Quest APK release feed. */
final class QuestReleasePolicy {
    static final String REPOSITORY = "SuperS92/GeneralsZH-QuestVR-Port";
    static final String API = "https://api.github.com/repos/" + REPOSITORY + "/releases/latest";
    static final long MAX_APK_BYTES = 512L * 1024 * 1024;

    static int compareVersions(String first, String second) {
        String[] a = parts(first), b = parts(second);
        for (int i = 0; i < 4; ++i) {
            int result = Integer.compare(i < a.length ? Integer.parseInt(a[i]) : 0,
                i < b.length ? Integer.parseInt(b[i]) : 0);
            if (result != 0) return result;
        }
        return 0;
    }

    private static String[] parts(String version) {
        if (version == null || !version.matches("[0-9]{1,4}(\\.[0-9]{1,4}){2,3}"))
            throw new IllegalArgumentException("Unsupported release version");
        return version.split("\\.");
    }

    static void validateAsset(String version, String name, String url, String digest, long size) {
        parts(version);
        if (!name.equals("GeneralsZH-QuestVR-" + version + ".apk")
            || !url.equals("https://github.com/" + REPOSITORY + "/releases/download/v"
                + version + "/" + name)
            || digest == null || !digest.matches("sha256:[0-9a-fA-F]{64}")
            || size < 1 || size > MAX_APK_BYTES)
            throw new IllegalArgumentException("Invalid Quest release asset");
    }

    static boolean allowedDownloadUrl(String value) {
        try {
            URI uri = new URI(value);
            String host = uri.getHost();
            return "https".equals(uri.getScheme()) && uri.getRawUserInfo() == null
                && (uri.getPort() == -1 || uri.getPort() == 443)
                && ("github.com".equals(host) || "release-assets.githubusercontent.com".equals(host)
                    || "objects.githubusercontent.com".equals(host));
        } catch (Exception invalid) { return false; }
    }
}

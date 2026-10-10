// SPDX-License-Identifier: GPL-3.0-or-later
package com.generalsx.zerohour;

public final class QuestReleasePolicyTest {
    private static void require(boolean value) { if (!value) throw new AssertionError(); }
    private static void reject(Runnable operation) {
        try { operation.run(); } catch (IllegalArgumentException expected) { return; }
        throw new AssertionError("Unsafe release accepted");
    }
    public static void main(String[] args) {
        String name = "GeneralsZH-QuestVR-1.4.0.7.apk";
        String url = "https://github.com/SuperS92/GeneralsZH-QuestVR-Port/releases/download/v1.4.0.7/" + name;
        String digest = "sha256:" + "a".repeat(64);
        QuestReleasePolicy.validateAsset("1.4.0.7", name, url, digest, 223000000);
        require(QuestReleasePolicy.compareVersions("1.4.0.7", "1.4.0.6") > 0);
        require(QuestReleasePolicy.compareVersions("1.4.0.10", "1.4.0.9") > 0);
        require(QuestReleasePolicy.compareVersions("1.5.0", "1.4.0.99") > 0);
        require(QuestReleasePolicy.compareVersions("1.4.0.6", "1.4.0.6") == 0);
        require(QuestReleasePolicy.compareVersions("1.4.0.5", "1.4.0.6") < 0);
        reject(() -> QuestReleasePolicy.compareVersions("1.4.0.7-beta", "1.4.0.6"));
        reject(() -> QuestReleasePolicy.compareVersions("999999999.1.1", "1.4.0.6"));
        reject(() -> QuestReleasePolicy.validateAsset("1.4.0.7", "phone.apk", url, digest, 2));
        reject(() -> QuestReleasePolicy.validateAsset("1.4.0.7", name,
            url.replace("SuperS92", "MYSOREZ"), digest, 2));
        reject(() -> QuestReleasePolicy.validateAsset("1.4.0.7", name, url, "", 2));
        reject(() -> QuestReleasePolicy.validateAsset("1.4.0.7", name, url, digest, 0));
        reject(() -> QuestReleasePolicy.validateAsset("1.4.0.7", name, url, digest, QuestReleasePolicy.MAX_APK_BYTES + 1));
        require(QuestReleasePolicy.allowedDownloadUrl(url));
        require(QuestReleasePolicy.allowedDownloadUrl("https://release-assets.githubusercontent.com/path?signature=test"));
        for (String bad : new String[] {"http://github.com/a", "https://github.com.evil.test/a",
            "https://evil.test/a", "https://github.com@evil.test/a", "file:///app.apk",
            "https://user@github.com/a", "https://github.com:8080/a", "bad"})
            require(!QuestReleasePolicy.allowedDownloadUrl(bad));
        System.out.println("PASS version ordering, release asset identity, digest/size bounds and redirect origins");
    }
}

# Quest APK updates

The launcher checks the latest published stable release of
`SuperS92/GeneralsZH-QuestVR-Port`. A check never starts a download or installation.
The player chooses Download, then Install and confirms Android's installer.
Android may first ask to allow installations from this launcher. App-private game
files and preferences survive an ordinary update with the same signing key.

Each release must include `GeneralsZH-QuestVR-<version>.apk`, an uploaded GitHub
SHA256 digest, and a matching `v<version>` tag. Before installation the launcher
checks the complete download size and hash, package ID, archive version, higher
version code and the installed app's exact signer set. Downloads only follow
HTTPS redirects to GitHub's release-asset hosts. Failed downloads never replace
the installed app. No account credentials are needed for this public feed.

The inherited Android engine updater remains disabled for Quest. Signed network
settings still come from MYSOREZ; GeneralsOnline game data still has its separate
update action and checksum checks. Future MYSOREZ engine changes need a reviewed
Quest build and maintainer-approved release, rather than an automatic phone
engine replacement.

The initial launcher with this feature needs the usual APK installation once.
Subsequent releases can be downloaded on the headset. Test the system installer
on a physical Quest before claiming the full update path is verified.

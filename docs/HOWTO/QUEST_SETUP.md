# Installation and PC crossplay

## Game files

Install the APK with SideQuest, or connect an authorised ADB device and run:

```powershell
adb install -r GeneralsZH-QuestVR-VERSION.apk
```

Copy both original game installation folders to a location the Quest file picker can access. Keep the base Generals folder and Zero Hour folder separate; do not merge their archives or Data folders.

In the launcher, import **Generals**, then **Zero Hour**. Select the roots of the installation folders. Wait for each import to finish. The originals are left alone; the app keeps its own copies.

The standalone Quest package is `com.generalsx.questvr`. It can coexist with the ordinary Android port. Updates signed with this project's release key keep imported data and settings. Uninstalling or clearing app storage removes that private data.

## GeneralsOnline

1. Install and update the [GeneralsOnline PC client](https://www.playgenerals.online/).
2. Run the PC client **without Easy Anti-Cheat**. Its built-in **GeneralsOnline AntiCheat may remain enabled**.
3. On the Quest, open the launcher account page and update online game data when offered.
4. Keep **Play with PC players** enabled and select **60 Hz simulation** in Graphics.
5. Sign in, host or join a custom match, and use the same current online data on both sides.

Completed PC/Quest matches have worked with hosting from either device. Quest/Android and Quest/Quest have not been tested. An online-data update cannot certify compatibility with every future PC client; report any new checksum or desync error with its full text and both versions.

## Graphics

Settings apply at the next game launch. To close both the game and launcher between comparisons:

```powershell
adb shell am force-stop com.generalsx.questvr
```

Start with the defaults. Lower render resolution from 1.5× to 1.0× if you need more FPS. Simulation rate and render FPS are different; retain 60 Hz simulation for PC crossplay. Some thin edges and distant textures still shimmer.

The launcher uses German on German systems and English otherwise. Campaign story videos and scripted camera scenes use a flat screen, then return to stereo gameplay.

## Updating on the headset

In the launcher, open **Start**, then the Updates card. Check for a new
version, download it and choose Install. Confirm Android's installation prompt;
it may first ask you to allow installations from this app. A normal update keeps
your saves, settings and imported game data. Do not uninstall before updating.

The updater offers the latest published stable release. Drafts and prereleases
are not offered. Installing from Unknown Sources is needed only for the initial
APK; later updates can be downloaded directly on the Quest.

## Invisible pointer or unit indicators

If the laser pointer or unit indicators become invisible or shrink to small
green dots, a clean reinstall may help. This recovered the indicators in headset
testing, but the underlying cause has not been established.

1. Back up your saves to a computer or another location outside this app's
   storage before uninstalling. Verify that the backup is readable. The saves
   are in the app's private `files/UserData/ZeroHour/Save` directory; accessing
   them requires an authorised ADB connection and a build that permits `run-as`.
   Do not uninstall if you cannot first retrieve saves you want to keep.
2. Keep your original Generals and Zero Hour folders available for import.
3. Uninstall the Quest app, then install the current release APK again.
4. Import both game folders and test the pointer before restoring saves.
5. Restore only the backed-up saves initially. Avoid copying old settings and
   caches back as part of this troubleshooting step.

**Uninstalling removes app data, settings, local account state and imported game
files.** Ordinary APK updates preserve them; try a clean reinstall only when
needed. Never post saves, game files, account data or full backups publicly.

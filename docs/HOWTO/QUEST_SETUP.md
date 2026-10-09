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

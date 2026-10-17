# Generals Zero Hour — Quest VR

A standalone Meta Quest port of **Command & Conquer: Generals – Zero Hour**. The game runs on the headset with OpenXR and Touch controllers.

Play above the battlefield, point to select units and give orders, build your base, and change your scale to get closer to the action. Tilt the battlefield for a more comfortable viewing angle. No gaming PC or Quest Link is needed to run it.

Thanks to the amazing Flat2VR Discord community for all the testing and feedback! So far, no major issues have been reported. During development, my main focus was skirmish and online play against PC players through [GeneralsOnline](https://www.playgenerals.online/). Campaign missions have also worked well in testing.

If you like what I came up with and want to support me feel free to buy me a Coffee. 
[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/M6X528HASO)


## Gameplay

[![Quest VR gameplay by NotAGameAddict](https://img.youtube.com/vi/_IzGGuUn1cY/hqdefault.jpg)](https://www.youtube.com/watch?v=_IzGGuUn1cY)

Gameplay video by [NotAGameAddict](https://www.youtube.com/@NotAGameAddict). Click the thumbnail to watch on YouTube.


## Tested so far

| Mode | Current experience |
| --- | --- |
| Skirmish | Works very well |
| Multiplayer Quest ↔ PC | Multiple completed matches through GeneralsOnline, hosting from either side |
| Campaign | Works well in the missions tested so far, including videos, scripted camera sequences and the transition to VR gameplay |
| Multiplayer Quest ↔ Android / Quest ↔ Quest | Not tested yet. Testers are welcome! |

Full campaigns and every multiplayer setup have not been tested. Large battles and demanding missions can still cause frame drops, but performance has improved significantly since the first release.

For better FPS, try lowering the render resolution to 1.0× or disabling shadows in the graphics settings. Framerate is currently capped at 60 FPS. The goal is to support 72 FPS in a future update while keeping PC crossplay compatible.

## Install

You need a Quest 3, Touch controllers, and your own **Generals and Zero Hour** game files. This project includes no EA game assets.

1. Download the APK from [Releases](../../releases) and sideload it with SideQuest or ADB.
2. Copy the original Generals and Zero Hour installation folders to the Quest, keeping them separate.
3. Open **Generals ZH Quest VR** from Unknown Sources. Import the Generals folder first, then the Zero Hour folder.
4. Start the game and try a skirmish.

The launcher copies the files into this app's private storage. You only need to import once; normal APK updates keep your data. See [installation and PC crossplay](docs/HOWTO/QUEST_SETUP.md) for details.

### Updates

From version **1.4.0.9** onward, you can download and install new releases directly on your Quest. Open **Start → Updates** in the launcher, check for updates and follow the download and installation prompts. No PC connection is needed.

You can also sideload a newer APK over your existing installation. **Do not uninstall first:** normal updates preserve your saves, settings and imported game files.

### Invisible pointer or unit indicators

If the laser pointer or unit indicators become invisible, a clean reinstall may help. **Back up your saves before uninstalling:** uninstalling removes app data and imported game files. You will need to import both game folders again. See the [reinstall guide](docs/HOWTO/QUEST_SETUP.md#invisible-pointer-or-unit-indicators).

## Controls and graphics

Use the right laser and trigger for menus, selection and orders. Hold the trigger and sweep across the ground to select a group. The left stick moves around the map; the right stick turns and changes height. The HUD sits on your left wrist. Hold the left grip while clicking or drawing a selection box to add units. Hold both grips and move the right stick up or down to tilt the battlefield in 15° steps.

See the [controller guide](docs/HOWTO/QUEST_CONTROLS.md) for building rotation, scaling, groups and the command wheel. The controls take inspiration from Gonzorro's GeneralsVR, with a few Quest-specific choices.

Defaults: 60 Hz simulation, 1.5× render resolution, TAA Lite, gentle tree contours, shadows and terrain batching on. Terrain and road filtering improvements are always enabled. Try 1.0× resolution for more performance. Detailed performance logging is off by default.

Campaign story videos and scripted camera sequences play on a flat screen. Normal gameplay returns to stereo VR automatically. Optional passthrough can show your room behind flat screens or the battlefield. Gameplay passthrough is experimental and reduces performance.

## Online play

Use the same current GeneralsOnline data on both devices, leave **Play with PC players** enabled, and use **60 Hz simulation** on the Quest. The PC must run **without Easy Anti-Cheat**. The built-in **GeneralsOnline AntiCheat may stay enabled**.

The launcher can update the online game data. That is separate from updating this Quest APK. Quick Match can start a search, but finding an opponent has not been verified on Quest; use Custom Match for tested PC crossplay.

## Feedback

The best way to ask questions and post suggestions is to use the [Flat2VR Modding discord](https://discord.com/channels/747967102895390741/1558218066016342226)

[Bug reports and feedback](../../issues) are welcome. Please include the app version, game mode and steps to reproduce the problem. Check logs for account details before attaching them; never upload game files or credentials.

I'd like to fix game-breaking bugs and add small comfort features I also find useful. Time is limited, so I can't promise major changes or new features, but feel free to ask.

## Credits

This builds on a lot of community work:

- **[MYSOREZ](https://github.com/MYSOREZ/GeneralsZH-Android-Port)** — the Android foundation, launcher and crossplay work.
- **[Gonzorro / GeneralsVR](https://github.com/Gonzorro/GeneralsVR)** — the PCVR port and controller interaction this Quest experience builds on.
- **[GeneralsOnline](https://www.playgenerals.online/)** — the online service and [PC client](https://github.com/GeneralsOnlineDevelopmentTeam/GameClient).
- **[TheSuperHackers](https://github.com/TheSuperHackers/GeneralsGameCode)** and **EA** — the released engine source and community maintenance.
- **[fbraz3 / GeneralsX](https://github.com/fbraz3/GeneralsX)**, **[Fighter19](https://github.com/Fighter19/CnC_Generals_Zero_Hour)** and **[jmarshall2323](https://github.com/jmarshall2323/CnC_Generals_Zero_Hour)** — earlier engine and platform work inherited through the Android port.

See [third-party credits](CREDITS.md) for libraries and licenses. The engine's [GPLv3 license and EA additional terms](LICENSE.md) remain in effect. This is an unofficial community project.

## Optional support

If you'd like to support my work buy me a coffee.
[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/M6X528HASO)

## Development

The Quest layer follows MYSOREZ v1.4.0 and preserves its Git ancestry. See the [build guide](docs/WORKDIR/quest/BUILD_RELEASE.md), [patch ledger](docs/WORKDIR/quest/PATCH_LEDGER.json), and [upstream update workflow](docs/WORKDIR/quest/UPSTREAM_SYNC.md).

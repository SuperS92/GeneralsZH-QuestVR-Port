# Generals Zero Hour — Quest VR

A standalone Meta Quest 3 port of **Command & Conquer: Generals – Zero Hour**, based on [MYSOREZ's Android port](https://github.com/MYSOREZ/GeneralsZH-Android-Port). 
The game runs on the headset with OpenXR and Touch controllers.

Play above the battlefield, point to select units and give orders, build your base, and change your scale to get closer to the action. No gaming PC or Quest Link is needed to run it.

**Early release.** I was working on this for myself for quite some time, quite a few attempts and code optimizations were needed to get it to this playable state where it is now. 
If you like what I came up with and want to support me feel free to buy me a Coffee [Ko-Fi](https://ko-fi.com/supers92). 
Until now it was only ever testet on my own Quest 3 device. Also the main focus for me was skirmish and online play against PC players through [GeneralsOnline](https://www.playgenerals.online/).

## Tested so far

| Mode | Current experience |
| --- | --- |
| Skirmish | Works well in my tests |
| Multiplayer Quest ↔ PC | Multiple completed matches through GeneralsOnline, hosting from either side |
| Campaign | USA and China briefly tested; videos, scripted camera sequences and the transition to VR gameplay work |
| Multiplayer Quest ↔ Android / Quest ↔ Quest | Not tested yet |

Full campaigns and every multiplayer setup have not been tested. Large battles and missions can have frame drops. I am still testing ways to get the Framerate up. For now it is possible to turn down the resolution to 1x in the settings to get better FPS.

## Install

You need a Quest 3, Touch controllers, and your own **Generals and Zero Hour** game files. This project includes no EA game assets.

1. Download the APK from [Releases](../../releases) and sideload it with SideQuest or ADB.
2. Copy the original Generals and Zero Hour installation folders to the Quest, keeping them separate.
3. Open **Generals ZH Quest VR** from Unknown Sources. Import the Generals folder first, then the Zero Hour folder.
4. Start the game and try a skirmish.

The launcher copies the files into this app's private storage. You only need to import once; normal APK updates keep your data. See [installation and PC crossplay](docs/HOWTO/QUEST_SETUP.md) for details.

## Controls and graphics

Use the right laser and trigger for menus, selection and orders. Hold the trigger and sweep across the ground to select a group. The left stick moves around the map; the right stick turns and changes height. The HUD sits on your left wrist.

See the [controller guide](docs/HOWTO/QUEST_CONTROLS.md) for building rotation, scaling, groups and the command wheel. The controls take inspiration from Gonzorro's GeneralsVR, with a few Quest-specific choices.

Defaults: 60 Hz simulation, 1.5× render resolution, AA off, gentle tree contours, shadows and terrain batching on. Terrain and road filtering improvements are always enabled. Try 1.0× resolution for more performance. Detailed performance logging is off by default.

Campaign story videos and scripted camera sequences play on a flat screen. Normal gameplay returns to stereo VR automatically.

## Online play

Use the same current GeneralsOnline data on both devices, leave **Play with PC players** enabled, and use **60 Hz simulation** on the Quest. The PC must run **without Easy Anti-Cheat**. The built-in **GeneralsOnline AntiCheat may stay enabled**.

The launcher can update the online game data. That is separate from updating this Quest APK.

## Feedback

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

If you'd like to support my work buy me a coffee [Ko-Fi](https://ko-fi.com/supers92). 

## Development

The Quest layer follows MYSOREZ v1.4.0 and preserves its Git ancestry. See the [build guide](docs/WORKDIR/quest/BUILD_RELEASE.md), [patch ledger](docs/WORKDIR/quest/PATCH_LEDGER.json), and [upstream update workflow](docs/WORKDIR/quest/UPSTREAM_SYNC.md).

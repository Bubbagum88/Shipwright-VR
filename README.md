# Ocarina of Time VR for Quest 1

The Legend of Zelda: Ocarina of Time in VR on the **original Oculus Quest (Quest 1)**, standalone. No PC or cable needed once it's installed.

This is a Quest 1 build of ChocoTorp's [Shipwright-VR](https://github.com/ChocoTorp/Shipwright-VR), the standalone Quest port of [Ship of Harkinian VR](https://github.com/ShinyWindow/Shipwright-VR) by ShinyWindow, which is built on [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) by HarbourMasters. It starts on the Quest 1 and adds a set of VR fixes and features, listed below.

**No game content is included.** You need your own Ocarina of Time ROM (`.z64`, `.n64` or `.v64`). Check it's a supported version at [ship.equipment](https://ship.equipment/).

## What "Quest 1 support" means

ChocoTorp's APK closes as soon as it starts on a Quest 1. Its manifest lists only newer headsets, and the Quest 1's VR runtime refuses an app that names a headset it doesn't know (`Unknown device to support: quest3`). This build lists `quest|quest2`. Nothing else was needed for the game to start on the Quest 1; [QUEST.md](QUEST.md#quest-1) has the details.

## Status

Tested on one Quest 1 (firmware v50) with release **quest1-v1.0**: the game runs and plays. That headset is rooted; nothing in the port uses root, but it hasn't been tried on an unrooted Quest 1.

## What's new compared with ChocoTorp's build

- Starts on the Quest 1.
- **Spin attack** with physical sword combat: squeeze the sword hand's trigger to charge (a buzz means full charge), then swing. It works while holding or right after letting go.
- **Bow:** the string pulls to your drawing hand and the arrow's back end sits in that hand.
- **Hookshot** fires straight down its barrel; item reticles (hookshot, bow, slingshot) are drawn in the world where the shot will land.
- **Enemy health bars** float over enemies in the world, in first and third person.
- **HUD** layout fixed for VR: the magic meter no longer overlaps the buttons.
- **Pause menu laser pointer:** point and pull the trigger to pick slots.
- **Shops:** pointer in first person, in-world cursor in third person.
- **First person:** get closer to NPCs; the camera keeps clear of walls; crawlspace exits fixed.
- **Item compass:** 3D models for the Master Sword and other items that showed a bottle or icon.
- **Movement stick deadzone** setting (VR Settings > Comfort & Movement).
- **Save editor** (Dev Tools): set all ammo to 10, 20, 30 or max.
- **Gauntlets:** no floating forearm pieces with motion-controlled hands.

## Requirements

- **Firmware:** tested on Quest 1 firmware **v50** only. Older firmware is untested.
- **Android:** minSdk 24 (Android 7). The Quest 1 on v50 runs Android 10.
- **Graphics:** OpenGL ES 3 with the `GL_OVR_multiview2` extension for single-pass stereo.
- **OpenXR:** the Khronos OpenXR loader 1.1.63 is bundled in the APK and uses the headset's own OpenXR runtime.

## Install

You need Developer Mode on the headset and either [SideQuest](https://sidequestvr.com/setup-howto) or `adb` (Android platform-tools) on your PC. [QUEST.md](QUEST.md) walks through Developer Mode and SideQuest step by step.

1. Download the APK from the [quest1-v1.0 release](https://github.com/Bubbagum88/Shipwright-VR/releases/tag/quest1-v1.0) and save it as `OoT-VR-quest1-v1.0.apk`.
2. Install it with SideQuest's **Install APK** button, or with adb.
3. Copy your ROM to the headset's **Download** folder. Any file name works.
4. In the headset, open **Quest 3: OOT** from **App Library > Unknown Sources**. When it asks, allow access to files, then go back to the game.
5. **The first launch shows loading dots for 1 to 2 minutes** while it builds the game data from your ROM. It isn't frozen, and this only happens once.

### The same steps with adb

In PowerShell, in your platform-tools folder (where `adb.exe` is), with the APK and your ROM saved there as `OoT-VR-quest1-v1.0.apk` and `oot.z64`:

```powershell
.\adb install OoT-VR-quest1-v1.0.apk
.\adb push oot.z64 /sdcard/Download/oot.z64
```

### Updating

Each build is signed with a test key, so a newer build won't install over an older one. Back up your saves first, uninstall, install the new APK, start it once, then put the saves back:

```powershell
.\adb pull /sdcard/Android/data/com.linkzenic.soh oot-backup
.\adb uninstall com.linkzenic.soh
.\adb install OoT-VR-quest1-v1.0.apk
.\adb push oot-backup/. /sdcard/Android/data/com.linkzenic.soh
```

## Known issues

- Thin dark lines can show above some trees in Hyrule Field and the Hyrule Castle grounds.

If something breaks, please [open an issue](https://github.com/Bubbagum88/Shipwright-VR/issues) with a log:

```powershell
.\adb logcat -d > oot-log.txt
```

## Texture pack

I play with **Djipi's 3DS Experience** on my Quest 1. It replaces the N64 textures with Ocarina of Time 3D's HD look and turns the pre-rendered rooms (Link's house, the Market, the shops) into real 3D rooms, which matters a lot in VR. It isn't included (it's Djipi's work, made from Nintendo's textures). Get it from [GameBanana](https://gamebanana.com/mods/477979) (`djipi_s_3ds_experience_tot_fix.zip`); [QUEST.md](QUEST.md) and [ChocoTorp's README](https://github.com/ChocoTorp/Shipwright-VR#bonuses) explain how to install it.

## Controls

| Action | Control |
| - | - |
| Move | Left stick |
| Turn | Right stick (smooth turning; snap turning is in the settings) |
| Sword | Swing your sword hand |
| Spin attack | Squeeze the sword hand's trigger to charge, then swing |
| Shield | Raise your shield hand |
| Choose an item | Click the right stick, flick your hand toward an item, release |
| Use an item | Trigger |
| Slingshot / bow | Bring your free hand to the weapon, squeeze the trigger, pull back, release |
| Grab and throw a Deku Nut | Grip near the nut in front of your chest, then throw |
| Pause, save, equip | Left stick click (right stick click in left-handed mode); point and pull the trigger to pick |
| Settings menu (in the headset) | Left menu button |

Everything VR-specific (left-handed mode, turning, hand and HUD placement, comfort, performance) is under **VR Settings** in the in-headset menu.

## Building

The [quest-apk workflow](.github/workflows/quest-apk.yml) builds the Quest 1 APK on GitHub Actions on pushes to this branch that touch code (or by hand from the **Actions** tab). For a local build, see [QUEST.md](QUEST.md#building-from-source).

## Credits

- **ChocoTorp** for the [Quest standalone port](https://github.com/ChocoTorp/Shipwright-VR).
- **ShinyWindow** for [Ship of Harkinian VR](https://github.com/ShinyWindow/Shipwright-VR) (motion controls, physical combat, the VR renderer).
- **HarbourMasters** for [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright).
- **linkzenic** for the Android port of Ship of Harkinian.
- **Djipi** for the 3DS Experience texture pack.
- Quest 1 build and the VR additions above: **Bubbagum88**.

More in [docs/CREDITS.md](docs/CREDITS.md). This is a fan project, not affiliated with or endorsed by Nintendo. The Legend of Zelda and Ocarina of Time are trademarks of Nintendo.

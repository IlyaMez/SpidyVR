# Linux (Steam Proton): experimental

Spidy is built for Windows. On Linux it runs inside the game's Proton session, started by
`tools/spidy-linux.sh` instead of Spidy Launcher.exe. Tested once: Spidy 0.3.2, game 4.630.0.0,
Proton Experimental, SteamVR on Linux with a Quest 3 over Steam Link, CachyOS, RX 6800.
Contributed by Sebi (sjrl2000). In the release zip since 0.3.6; that build has not run under Proton
yet (the tested session used 0.3.2 with the lock change below patched in by hand).
WiVRn: tested once with 0.3.6, WiVRn 26.9 (Flatpak) with a Quest 3, Proton Experimental. Steam Link
stayed the more reliable of the two.

## Setup

1. Extract the Spidy zip anywhere, for example `~/Spidy`.
2. In Steam: right-click the game > Properties > General > Launch Options:

       STEAM_COMPAT_LAUNCHER_SERVICE=proton %command%

   It lets programs run inside the game's Proton session; the game itself is unchanged.
3. WiVRn only: let the game's container load WiVRn too. With WiVRn's Flatpak build (as WiVRn
   recommends, tested), the launch options become

       PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES=1 PRESSURE_VESSEL_FILESYSTEMS_RW=/var/lib/flatpak/app/io.github.wivrn.wivrn STEAM_COMPAT_LAUNCHER_SERVICE=proton %command%

   That path is for a system-wide Flatpak install. For a `--user` install use
   `PRESSURE_VESSEL_FILESYSTEMS_RW=/home/<you>/.local/share/flatpak/app/io.github.wivrn.wivrn`
   with your full home path. `flatpak info io.github.wivrn.wivrn`
   shows which one you have under Installation. A WiVRn installed from your distribution's packages
   does not need `PRESSURE_VESSEL_FILESYSTEMS_RW`.
   Without these the container does not see WiVRn. With SteamVR the short options above are the
   tested ones.
4. Install Microsoft's Visual C++ runtime into the game's Proton prefix, once (start the game once
   first, so that the prefix exists):

       protontricks 1817070 -q vcrun2017 vcrun2022

   With the Flatpak protontricks: `flatpak run com.github.Matoking.protontricks 1817070 -q vcrun2017
   vcrun2022`. Spidy's modules need version 14.40 or newer. On Windows the launcher installs it; here
   nothing does, and Wine's own stand-in is not enough. Without it the scripts found no headset, with
   WiVRn and SteamVR alike (GitHub #50; three players on Discord in November). Builds after 0.8.7
   say so before they look for the headset.
5. SteamVR over Steam Link: SteamVR's beta branch (Steam > SteamVR > Properties > Betas) has worked
   better. If a firewall runs, open Steam's streaming ports, for example with ufw:

       sudo ufw allow 27031/udp
       sudo ufw allow 27036/udp
       sudo ufw allow 27036:27037/tcp
       sudo ufw reload

## Play

1. Start SteamVR and connect the headset (SteamVR home showing), or start WiVRn and connect the
   headset in the WiVRn app. Do this before the game: Proton sets up its OpenXR bridge only when the
   game starts.
   A USB cable instead of Wi-Fi worked with both runtimes too (Steam Link's wired mode, and the wired
   setting in WiVRn's Linux app): turn on Developer Mode for the Quest (in the Meta Horizon phone
   app), then allow USB debugging in the headset when you plug it in.
2. Start the game from Steam and wait at the main menu.
3. From the Spidy folder run `bash tools/spidy-steamvr.sh` or `bash tools/spidy-wivrn.sh` (a zip
   made on Windows keeps no run permission for the scripts). Each checks its runtime first;
   `tools/spidy-linux.sh` uses whichever runtime is active.
4. Click the game window once, load your save with mouse or keyboard, and VR takes over in gameplay.

## When it does not work

- `The Microsoft Visual C++ runtime here is ...` or `is Wine's own`, or no headset found although
  `PROTON_LOG=1` shows the runtime finding it: setup step 4 (`protontricks 1817070 -q vcrun2017
  vcrun2022`), then start the game again from Steam.
- `No headset found (wineopenxr64: OpenXR instance unavailable: -51)`: Proton found no VR runtime
  when the game started. Quit the game, make sure SteamVR or WiVRn has the headset, start the game
  again, then the script. With WiVRn, check that `~/.config/openxr/1/active_runtime.json` names
  WiVRn and that `~/.config/openvr/openvrpaths.vrpath` lists WiVRn's xrizer (WiVRn writes both
  while it runs; after a WiVRn update a stale xrizer path is fixed by restarting WiVRn).
- To see what Proton found, add `PROTON_LOG=1` to the launch options and start the game: in
  `~/steam-1817070.log`, a working setup shows xrizer and the headset (for WiVRn, `WiVRn HMD`) next to
  `wineopenxr test instance`. Remove `PROTON_LOG=1` afterwards: it slows the game.
- The scripts warn when the game was started before SteamVR or WiVRn (the usual cause of -51), and
  when WiVRn is used but the game was started without `PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES=1`
  or, for the Flatpak, without `PRESSURE_VESSEL_FILESYSTEMS_RW` sharing WiVRn's folder (the
  container then cannot read WiVRn's manifest: -51 even when the start order is right).
- Back to SteamVR after WiVRn: WiVRn deletes `~/.config/openxr/1/active_runtime.json` when it exits,
  and SteamVR does not recreate it, so the game finds no runtime. When the file is missing or still
  names WiVRn, `spidy-steamvr.sh` points it at SteamVR's `steamxr_linux64.json` (in whichever Steam
  library has SteamVR) and asks you to restart the game; another runtime, such as Monado, is left
  alone with a warning. SteamVR Settings > OpenXR > "Set SteamVR as OpenXR Runtime" does the same.
- WiVRn: stepping outside the Quest's boundary stops the game until you are back inside. The headset
  shows passthrough and the session loses focus, and Spidy reads no input without focus. Make the
  boundary larger, use a stationary boundary, or (developer mode, over USB)
  `adb shell setprop debug.oculus.guardian_pause 0` until the headset restarts; that also drops the
  boundary's warning, so clear the room.

## Why not Spidy Launcher.exe

- Run on its own (for example with protontricks-launch), the launcher is outside the game's
  session: Proton's OpenXR bridge (wineopenxr) finds no headset, and the game cannot be reached.
- START VR starts the game through `steam.exe -applaunch`, which does not work from inside Proton.
  The script attaches to the game Steam started instead.

## Known limits

- Attaching to a running game, Spidy cannot enlarge its render memory (the launcher does that in
  the first seconds): heavy scenes may drop eye frames.
- Menus do not read the VR controllers (SteamVR's virtual gamepads); use mouse or keyboard there,
  or try Properties > Controller > Disable Steam Input.
- Quitting to the main menu may close the game (Steam ticket guard 9803 under Proton).
- Spidy sets the game's settings for VR only in a game it starts itself. Here, set them in the game
  before you play: frame generation and vertical sync off, Ray-Traced Reflections off, Aspect Ratio
  at Auto.

## What was changed for Proton

- `render_probe.cpp`: Proton's DXGI (DXVK/vkd3d-proton) has no composition swapchains, so finding
  the game's queue failed with 2104. It now falls back to a swapchain on a hidden window.
- `native_eye_gpu.cpp`: vkd3d-proton uses one function for every command list, so the ResourceBarrier,
  Reset and EndQuery hooks also see Spidy's own calls and re-entered while `recording` was held. The
  game closed with resource_deadlock_would_occur. `recording` is now a recursive mutex.

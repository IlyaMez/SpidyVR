#!/usr/bin/env bash
# Start Spidy VR inside a running Spider-Man Remastered Proton session.
# Needs: Steam launch option  STEAM_COMPAT_LAUNCHER_SERVICE=proton %command%
#   (WiVRn: put PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES=1 in front, and for its Flatpak also
#    PRESSURE_VESSEL_FILESYSTEMS_RW=/var/lib/flatpak/app/io.github.wivrn.wivrn, or for a --user
#    install ~/.local/share/flatpak/app/io.github.wivrn.wivrn written out in full; see docs/LINUX.md)
# Order: start SteamVR or WiVRn, connect the headset, start the game from Steam, wait at the main menu, run this.
set -euo pipefail

# Default: the Spidy folder this script is in (tools/ sits next to python/ and build/).
SPIDY_DIR="${SPIDY_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
BUS=com.steampowered.App1817070

# Steam libraries: the default one and those in libraryfolders.vdf.
libraries=("$HOME/.local/share/Steam/steamapps")
vdf="$HOME/.local/share/Steam/steamapps/libraryfolders.vdf"
[[ -f "$vdf" ]] && while IFS= read -r path; do libraries+=("$path/steamapps"); done \
    < <(sed -n 's/^\s*"path"\s*"\(.*\)"/\1/p' "$vdf")

# steam-runtime-launch-client from any Steam library's Linux runtime.
CLIENT="${SPIDY_LAUNCH_CLIENT:-}"
if [[ -z "$CLIENT" ]]; then
    for lib in "${libraries[@]}"; do
        for runtime in SteamLinuxRuntime_4 SteamLinuxRuntime_sniper SteamLinuxRuntime_soldier; do
            candidate="$lib/common/$runtime/pressure-vessel/bin/steam-runtime-launch-client"
            if [[ -x "$candidate" ]]; then CLIENT="$candidate"; break 2; fi
        done
    done
fi
[[ -x "$CLIENT" ]] || { echo "steam-runtime-launch-client not found (set SPIDY_LAUNCH_CLIENT)." >&2; exit 1; }
[[ -f "$SPIDY_DIR/tools/run_game_vr.py" ]] || { echo "Spidy not found in $SPIDY_DIR (set SPIDY_DIR)." >&2; exit 1; }

# Proton's OpenXR bridge uses the runtime the host's active_runtime.json names, and sets it up
# only when the game starts: the runtime has to be running and selected before the game starts.
# SPIDY_RUNTIME=steamvr|wivrn (set by spidy-steamvr.sh / spidy-wivrn.sh); default: the active one.
ACTIVE="${XDG_CONFIG_HOME:-$HOME/.config}/openxr/1/active_runtime.json"
STEAMVR_JSON=""
for lib in "${libraries[@]}"; do
    if [[ -f "$lib/common/SteamVR/steamxr_linux64.json" ]]; then STEAMVR_JSON="$lib/common/SteamVR/steamxr_linux64.json"; break; fi
done
RUNTIME="${SPIDY_RUNTIME:-}"
if [[ -z "$RUNTIME" ]]; then
    if grep -qi wivrn "$ACTIVE" 2>/dev/null; then RUNTIME=wivrn; else RUNTIME=steamvr; fi
fi
game=$(pgrep -o -f 'Spider-Man\.exe' || true)

# Warns when the game started before the VR server (Proton then has no VR bridge: error -51).
check_start_order() {
    local server=$1 name=$2 game_age server_age
    [[ -n "$game" && -n "$server" ]] || return 0
    game_age=$(ps -o etimes= -p "$game" | tr -d ' ')
    server_age=$(ps -o etimes= -p "$server" | tr -d ' ')
    if [[ -n "$game_age" && -n "$server_age" ]] && (( game_age > server_age )); then
        echo "Warning: the game was started before $name (or $name restarted since). Close the game and start it again from Steam." >&2
    fi
}

case "$RUNTIME" in
steamvr)
    if pgrep -x wivrn-server >/dev/null; then
        echo "WiVRn is running and owns the OpenXR runtime. Close WiVRn, then start SteamVR and the game." >&2
        exit 1
    fi
    # WiVRn deletes active_runtime.json when it exits, and SteamVR does not recreate it.
    # Point it at SteamVR only when it is missing or still names WiVRn; leave other runtimes alone.
    if [[ ! -e "$ACTIVE" ]] || grep -qi wivrn "$ACTIVE" 2>/dev/null; then
        if [[ -z "$STEAMVR_JSON" ]]; then
            echo "No OpenXR runtime is selected and SteamVR's steamxr_linux64.json was not found. In SteamVR Settings > OpenXR, click \"Set SteamVR as OpenXR Runtime\", then start the game again." >&2
            exit 1
        fi
        mkdir -p "$(dirname "$ACTIVE")"
        ln -sfn "$STEAMVR_JSON" "$ACTIVE"
        echo "SteamVR is now the OpenXR runtime ($ACTIVE -> $STEAMVR_JSON). Close the game, start it again from Steam, then run this again." >&2
        exit 1
    elif ! grep -q '"VALVE_runtime_is_steamvr"' "$ACTIVE" 2>/dev/null; then
        echo "Warning: the active OpenXR runtime ($ACTIVE) is neither SteamVR nor WiVRn; left as it is. To use SteamVR, select it in SteamVR Settings > OpenXR." >&2
    fi
    server=$(pgrep -ox vrserver || true)
    [[ -n "$server" ]] || echo "Warning: SteamVR does not seem to be running. Start it and connect the headset first." >&2
    check_start_order "$server" SteamVR
    ;;
wivrn)
    server=$(pgrep -ox wivrn-server || true)
    if [[ -z "$server" ]]; then
        echo "WiVRn is not running. Start it, connect the headset, then start the game from Steam." >&2
        exit 1
    fi
    grep -qi wivrn "$ACTIVE" 2>/dev/null || echo "Warning: WiVRn is running but is not the active OpenXR runtime ($ACTIVE)." >&2
    # pressure-vessel hides the host's runtime from the game unless asked to import it.
    # Any process matching the exe (Steam's reaper, Proton, the game) may carry the launch options.
    imported=0
    for pid in $(pgrep -f 'Spider-Man\.exe' || true); do
        if grep -qz '^PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES=1$' "/proc/$pid/environ" 2>/dev/null; then imported=1; break; fi
    done
    if [[ -n "$game" ]] && (( ! imported )); then
        echo "Warning: the game was not started with PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES=1 in its launch options; it may not find WiVRn." >&2
    fi
    # The Flatpak's manifest is outside what the container sees unless PRESSURE_VESSEL_FILESYSTEMS_RW shares it.
    manifest=$(readlink -f "$ACTIVE" 2>/dev/null || true)
    if [[ -n "$game" && "$manifest" == */flatpak/app/io.github.wivrn.wivrn/* ]]; then
        flatpak_dir="${manifest%%/io.github.wivrn.wivrn/*}/io.github.wivrn.wivrn"
        shared=0
        for pid in $(pgrep -f 'Spider-Man\.exe' || true); do
            rw=$(grep -z '^PRESSURE_VESSEL_FILESYSTEMS_RW=' "/proc/$pid/environ" 2>/dev/null | tr -d '\0' || true)
            IFS=: read -ra dirs <<< "${rw#*=}"
            for dir in "${dirs[@]}"; do
                [[ -n "$dir" && ( "$flatpak_dir" == "${dir%/}" || "$flatpak_dir" == "${dir%/}"/* ) ]] && { shared=1; break 2; }
            done
        done
        if (( ! shared )); then
            echo "Warning: the game was not started with PRESSURE_VESSEL_FILESYSTEMS_RW=$flatpak_dir in its launch options; it cannot read WiVRn's Flatpak (error -51)." >&2
        fi
    fi
    check_start_order "$server" WiVRn
    ;;
*)
    echo "Unknown SPIDY_RUNTIME '$RUNTIME' (use steamvr or wivrn)." >&2
    exit 1
    ;;
esac

if ! "$CLIENT" --list 2>/dev/null | grep -qx -- "--bus-name=$BUS"; then
    cat >&2 <<MSG
The game's Proton session was not found. Check that:
  1. Spider-Man's Steam launch options are:  STEAM_COMPAT_LAUNCHER_SERVICE=proton %command%
  2. the game was started from Steam and is at the main menu.
MSG
    exit 1
fi

echo "Starting Spidy VR. Click the game window once, then load your save (mouse or keyboard)."
cd "$SPIDY_DIR"

# run_game_vr.py takes the SPIDY VR tab settings (webs, flips, haptics, ...) only as CLI flags and
# has no memory of its own; Spidy Launcher.exe supplies that memory on Windows. Here, a persisted
# settings file plays the same part: PERSISTED_ARGS carries last session's settings in, and the
# session's own 'VR settings from the headset: ...' line (printed when they changed) is saved back
# out once it exits. Without a Python 3 on PATH, settings just reset to defaults each session.
PERSISTED_ARGS=()
if command -v python3 >/dev/null; then
    mapfile -t PERSISTED_ARGS < <(python3 "$SPIDY_DIR/tools/vr_settings_persist.py" load 2>/dev/null || true)
fi

SESSION_LOG=$(mktemp)
trap 'rm -f "$SESSION_LOG"' EXIT

set +e
"$CLIENT" --bus-name="$BUS" --directory="$SPIDY_DIR" --unset-env=XR_RUNTIME_JSON \
    -- wine python/python.exe -u tools/run_game_vr.py --xr-runtime auto "${PERSISTED_ARGS[@]}" "$@" \
    2> >(grep --line-buffered -v -E 'Fontconfig|fixme:' >&2) \
    | tee "$SESSION_LOG"
status=${PIPESTATUS[0]}
set -e

if command -v python3 >/dev/null; then
    settings_line=$(grep -F 'VR settings from the headset: ' "$SESSION_LOG" | tail -n1 || true)
    [[ -n "$settings_line" ]] && python3 "$SPIDY_DIR/tools/vr_settings_persist.py" save "$settings_line"
fi

exit "$status"

"""The OpenXR runtimes on this PC, and the one a VR session uses.

Spidy has been played through Virtual Desktop on a Quest 3, and through SteamVR (Steam Link) on a
Quest 3 since October 7. Other runtimes (Meta Quest Link, SteamVR headsets) start the same way and
offer the same controller bindings. A session uses the runtime its headset is connected to (detect)
unless one is asked for. Run as a script, it lists the runtimes as JSON; with --detect it finds the
headset the way a session does.
"""
import argparse
import json
import os
import pathlib
import subprocess
import sys
import time
import winreg

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROBE = ROOT/'build/windows-ninja/spidy_headset_probe.exe'
KHRONOS = r'SOFTWARE\Khronos\OpenXR\1'
VIRTUAL_DESKTOP = (pathlib.Path(os.environ.get('ProgramFiles', r'C:\Program Files'))/
                   'Virtual Desktop Streamer/OpenXR/virtualdesktop-openxr.json')
# Where OpenVR programs read SteamVR's folder from.
OPENVR_PATHS = pathlib.Path(os.environ.get('LOCALAPPDATA', ''))/'openvr/openvrpaths.vrpath'
# Recognisable runtimes by a piece of their manifest path; the manifest's own name is the fallback.
KNOWN = (('virtualdesktop', 'Virtual Desktop'), ('steamxr', 'SteamVR'), ('oculus', 'Meta Quest Link'),
         ('mixedreality', 'Windows Mixed Reality'), ('pimax', 'Pimax'), ('varjo', 'Varjo'),
         ('wivrn', 'WiVRn'), ('vive', 'VIVE'))
# A runtime's VR service, which runs while it is in use. Asking a runtime for a headset can start its
# app (asking SteamVR starts SteamVR), so detect asks Virtual Desktop (which starts nothing), runtimes
# whose service runs, and Windows' active runtime. SteamVR's server runs only while SteamVR does; Meta's
# runs whenever its app is installed, so SteamVR is asked first.
SERVICES = (('SteamVR', 'vrserver.exe'), ('Meta Quest Link', 'ovrserver_x64.exe'))
NONE_INSTALLED = ('No OpenXR runtime is installed. Install Virtual Desktop (the tested way to play), SteamVR or '
                  'the Meta Quest Link app.')


def _machine_key(path):
    return winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, path, 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY)


def steamvr(paths=OPENVR_PATHS):
    """SteamVR's OpenXR manifests, by the folders OpenVR's path file lists as its runtime.

    The Khronos registry names SteamVR only while it is Windows' active runtime: with Virtual Desktop
    or the Meta Quest Link app active, a headset on Steam Link had no SteamVR to choose or to ask.
    """
    try:
        folders = json.loads(pathlib.Path(paths).read_text(encoding='utf-8-sig'))['runtime']
    except (OSError, ValueError, KeyError, TypeError):
        return []
    if not isinstance(folders, list):
        return []
    found = []
    for folder in folders:
        if isinstance(folder, str):
            # As Windows spells it: the file keeps a lower-case copy of the folder too.
            manifest = pathlib.Path(folder)/'steamxr_win64.json'
            found.append(str(manifest.resolve() if manifest.is_file() else manifest))
    return found


def registered():
    """(active manifest or None, every manifest the Khronos registry lists, then SteamVR's own)."""
    active, available = None, []
    try:
        with _machine_key(KHRONOS) as key:
            active = winreg.QueryValueEx(key, 'ActiveRuntime')[0] or None
    except OSError:
        pass
    try:
        with _machine_key(KHRONOS+r'\AvailableRuntimes') as key:
            for index in range(winreg.QueryInfoKey(key)[1]):
                available.append(winreg.EnumValue(key, index)[0])
    except OSError:
        pass
    return active, available+steamvr()


def name(manifest):
    lowered = str(manifest).lower()
    for piece, label in KNOWN:
        if piece in lowered:
            return label
    try:
        return json.loads(pathlib.Path(manifest).read_text(encoding='utf-8'))['runtime']['name']
    except (OSError, ValueError, KeyError, TypeError):
        return pathlib.Path(manifest).stem


def runtimes(listing=registered, virtual_desktop=VIRTUAL_DESKTOP):
    """Installed runtimes, the tested one first: dicts of manifest, name, active, tested."""
    active, available = listing()
    seen, found = set(), []
    for manifest in (str(virtual_desktop), active, *available):
        if not manifest or os.path.normcase(manifest) in seen or not pathlib.Path(manifest).is_file():
            continue
        seen.add(os.path.normcase(manifest))
        found.append(dict(manifest=manifest, name=name(manifest),
                          active=bool(active) and os.path.normcase(manifest) == os.path.normcase(active),
                          tested=os.path.normcase(manifest) == os.path.normcase(str(virtual_desktop))))
    return found


def choose(requested=None, listing=registered, virtual_desktop=VIRTUAL_DESKTOP):
    """The manifest asked for, else the one detect asks first: Virtual Desktop, else Windows' active one."""
    if requested:
        if not pathlib.Path(requested).is_file():
            raise RuntimeError(f'The OpenXR runtime {requested} is not installed.')
        return pathlib.Path(requested)
    installed = runtimes(listing, virtual_desktop)
    if not installed:
        raise RuntimeError(NONE_INSTALLED)
    preferred = next((r for r in installed if r['tested']), None) or next((r for r in installed if r['active']), None)
    return pathlib.Path((preferred or installed[0])['manifest'])


def probe(manifest, timeout=60, program=PROBE):
    """Ask one runtime for a headset with Spidy's headset probe, which starts no session.

    Returns (found, what the probe said). A runtime that is not running yet may start meanwhile
    (SteamVR takes up to half a minute), hence the long timeout.
    """
    if not program.is_file():
        raise RuntimeError('Build the headset probe first')
    try:
        result = subprocess.run([str(program)], env={**os.environ, 'XR_RUNTIME_JSON': str(manifest)},
                                capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, f'no answer within {timeout} seconds'
    said = result.stdout if result.returncode == 0 else (result.stderr or result.stdout)
    return result.returncode == 0, said.strip()


def candidates(installed, running):
    """The runtimes detect asks, in order: Virtual Desktop, those whose VR service runs, Windows' active one."""
    order = [r for r in installed if r['tested']]
    for label, service in SERVICES:
        order += [r for r in installed if r['name'] == label and service in running and r not in order]
    return order+[r for r in installed if r['active'] and r not in order]


def detect(listing=registered, virtual_desktop=VIRTUAL_DESKTOP, ask=probe, running=None, say=None, settle=5):
    """The runtime a headset is connected to now: (manifest, what its probe said).

    `say` hears which runtime is asked next. A runtime whose service was not running (Windows' active
    one, which asking starts) is asked again three times, `settle` seconds apart, while its headset
    shows up. Raises naming every runtime asked and its answer when none has a headset.
    """
    installed = runtimes(listing, virtual_desktop)
    if not installed:
        raise RuntimeError(NONE_INSTALLED)
    if running is None:
        from capture_game_state import running_programs
        running = running_programs()
    answers = []
    for runtime in candidates(installed, running):
        if say:
            say(f"Looking for the headset in {runtime['name']}...")
        found, said = ask(runtime['manifest'])
        service = dict(SERVICES).get(runtime['name'])
        for _ in range(3 if not found and settle and service and service not in running else 0):
            if say:
                say(f"Waiting for {runtime['name']} to find the headset...")
            time.sleep(settle)
            found, said = ask(runtime['manifest'])
            if found:
                break
        if found:
            return pathlib.Path(runtime['manifest']), said
        answers.append(f"{runtime['name']}: {said.splitlines()[-1] if said else 'no headset'}")
    others = [r['name'] for r in installed if r not in candidates(installed, running)]
    asked = '; '.join(answers) or 'no runtime could be asked without starting it'
    raise RuntimeError(f'No headset found ({asked}).' + (f" Not asked, since they are not running: "
                                                       f"{', '.join(others)}." if others else '') +
                       ' Connect your headset in its VR app (Virtual Desktop, SteamVR, Meta Quest Link) and start '
                       'again, or choose the runtime in the launcher.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--detect', action='store_true', help='find the runtime a headset is connected to')
    a = p.parse_args()
    if not a.detect:
        json.dump(runtimes(), sys.stdout, indent=2)
        print()
        return 0
    try:
        manifest, said = detect(say=lambda line: print(line, flush=True))
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 3
    print(f'VR runtime: {name(manifest)} ({manifest}), found automatically.')
    print(said)
    return 0


if __name__ == '__main__':
    sys.exit(main())

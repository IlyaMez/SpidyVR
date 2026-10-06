"""The OpenXR runtimes on this PC, and the one a VR session uses.

Spidy has been played through Virtual Desktop on a Quest 3. Other runtimes (SteamVR, Meta Quest
Link) start the same way and offer the same controller bindings, but no session has used them yet.
Run as a script, it lists the runtimes as JSON.
"""
import json
import os
import pathlib
import sys
import winreg

KHRONOS = r'SOFTWARE\Khronos\OpenXR\1'
VIRTUAL_DESKTOP = (pathlib.Path(os.environ.get('ProgramFiles', r'C:\Program Files'))/
                   'Virtual Desktop Streamer/OpenXR/virtualdesktop-openxr.json')
# Recognisable runtimes by a piece of their manifest path; the manifest's own name is the fallback.
KNOWN = (('virtualdesktop', 'Virtual Desktop'), ('steamxr', 'SteamVR'), ('oculus', 'Meta Quest Link'),
         ('mixedreality', 'Windows Mixed Reality'), ('pimax', 'Pimax'), ('varjo', 'Varjo'),
         ('wivrn', 'WiVRn'), ('vive', 'VIVE'))


def _machine_key(path):
    return winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, path, 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY)


def registered():
    """(active manifest or None, every manifest the Khronos registry lists)."""
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
    return active, available


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
    """The manifest a session uses: the one asked for, else Virtual Desktop, else Windows' active one."""
    if requested:
        if not pathlib.Path(requested).is_file():
            raise RuntimeError(f'The OpenXR runtime {requested} is not installed.')
        return pathlib.Path(requested)
    installed = runtimes(listing, virtual_desktop)
    if not installed:
        raise RuntimeError('No OpenXR runtime is installed. Install Virtual Desktop (the tested way to play), '
                           'SteamVR or the Meta Quest Link app.')
    preferred = next((r for r in installed if r['tested']), None) or next((r for r in installed if r['active']), None)
    return pathlib.Path((preferred or installed[0])['manifest'])


if __name__ == '__main__':
    json.dump(runtimes(), sys.stdout, indent=2)
    print()

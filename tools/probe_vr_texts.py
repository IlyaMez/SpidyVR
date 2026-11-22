"""The game's texts in VR wording (vr_texts.hpp) in the running game, no headset.

    python tools/probe_vr_texts.py [--restarts 3] [--seconds 20]
    python tools/close_game.py

Launches the game as `probe_menu_pad.py start` does, starts the menu's hooks (which carry the hook on the game's
text table) and the prompt reading, and at the title screen runs `SpidyVrTextsCheck`: every entry of the table
through the game's own lookups, by hash and by tag, with the wording off and on. Then turns the wording on, presses
A until a player exists (CONTINUE), restarts the last checkpoint `--restarts` times for more loading tips, and
collects what the reading saw: the `VR text TAG` lines (a text the game was given in VR wording) and the ShowText
calls that carried one. Screenshots every second through each load go to reports/vr-texts/. The game is left
running; back up the saves first (Continue re-saves them).
"""
import argparse
import json
import pathlib
import struct
import sys
import time

import probe_menu_pad as pad
import probe_prompt_watch as watch
from observe_game import call_with_payload
from probe_menu import FLAGS, settings_payload

EXPORTS = watch.EXPORTS + ('SpidyPadSample', 'SpidyPlayerFind', 'SpidyVrTextsSet', 'SpidyVrTextsCheck')
CHECK = 112  # game_prompts::TextsCheck


def check(game, process, xr):
    code, raw = pad.call_with_output(game, process, xr['SpidyVrTextsCheck'], b'', CHECK)
    if code or len(raw) != CHECK:
        return dict(error=f'SpidyVrTextsCheck: {code}')
    magic, version, size, status = struct.unpack_from('<4I', raw)
    if (magic, version, size) != (0x54585456, 1, CHECK):
        return dict(error=f'TextsCheck mismatch: {hex(magic)} v{version} {size}')
    out = dict(zip(('entries', 'english', 'kept_by_hash', 'kept_by_tag', 'vr_by_hash', 'vr_by_tag'),
                   struct.unpack_from('<6I', raw, 16)))
    out['first_miss'] = raw[48:112].split(b'\0', 1)[0].decode('utf-8', 'replace')
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--dll', default=str(pad.XR_DLL), help='the stereo module to load')
    p.add_argument('--restarts', type=int, default=3, help='checkpoint restarts after the load, for more tips')
    p.add_argument('--seconds', type=float, default=20, help='free roam watched after each load')
    p.add_argument('--output', default=str(pad.ROOT/'reports/vr-texts.json'))
    p.add_argument('--attach', action='store_true', help='the game already running with the modules in it')
    p.add_argument('--watch', type=float, default=0,
                   help='press nothing: watch this many seconds (screenshots every 2 s) while someone else drives '
                        'the game, e.g. probe_menu_pad.py pad ... through a new game\'s first mission')
    a = p.parse_args()
    try:
        pad.user32.SetProcessDpiAwarenessContext(pad.c.c_void_p(-4))
    except AttributeError:
        pad.user32.SetProcessDPIAware()
    pad.XR_DLL = pathlib.Path(a.dll)
    pad.OUTPUT = pad.ROOT/'reports/vr-texts'/time.strftime('%Y%m%d-%H%M%S')
    out = dict(dll=str(pad.XR_DLL), events=[], vr_texts=[], show_texts=[], shots=[], died=False)
    started = time.monotonic()
    seen_lines = set()

    def event(text, **more):
        out['events'].append(dict(seconds=round(time.monotonic() - started, 1), event=text, **more))
        print(json.dumps(out['events'][-1]), flush=True)

    def save():
        pathlib.Path(a.output).parent.mkdir(parents=True, exist_ok=True)
        pathlib.Path(a.output).write_text(json.dumps(out, indent=1), encoding='utf-8')

    loaded = {}
    if a.attach:
        game = pad.Game(pad.find_game())
    else:
        game = pad.wait_for_game(prepare=lambda: pad.vr_display.prepare_launch(small=True),
                                 early=lambda pid: loaded.update(pad.enlarge_render_memory(pid)),
                                 ready=lambda g: True)
    process = pad.open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)

    def collect():
        seen = watch.watched(game, process, xr)
        out['watched'] = {k: seen.get(k) for k in ('reading', 'invokes', 'calls', 'texts', 'rewrites', 'by_name')}
        for line in seen.get('timeline', []):
            # Each line is "<milliseconds> <what>", the stamp right-aligned.
            stamp, _, body = line.strip().partition(' ')
            body = body.strip() if stamp.isdigit() else line.strip()
            if body in seen_lines:
                continue
            seen_lines.add(body)
            if body.startswith('VR text '):
                out['vr_texts'].append(body[8:])
                event('VR text', tag=body[8:])
            elif body.startswith('ShowText('):
                out['show_texts'].append(body)
                event('ShowText', call=body)

    def snap(label):
        path, mean, front = pad.shot(game, f'{len(out["shots"]):03d}-{label}')
        out['shots'].append(dict(seconds=round(time.monotonic() - started, 1), file=str(path), front=front))

    def watch_until(done, label, limit, tips=False):
        until = time.monotonic() + limit
        last_shot = last_tip = 0.0
        while time.monotonic() < until:
            if not pad.alive(game):
                out['died'] = True
                event('the game is gone', phase=label)
                return False
            collect()
            if time.monotonic() - last_shot >= 1:
                last_shot = time.monotonic()
                snap(label)
            # The loading screen's NEXT TIP is Y: a press every few seconds shows more of its tips.
            if tips and time.monotonic() - last_tip >= 2.5:
                last_tip = time.monotonic()
                pad.pad(process, xr, pad.BUTTONS['y'], 120)
            if done():
                return True
            save()
            time.sleep(.25)
        return False

    try:
        if a.attach:
            event('attached', pid=game.pid, in_front=pad.bring_to_front(game.pid))
        else:
            pad.wait_for_renderer(game, process, loaded.get('SpidyRenderMemoryData'))
            event('renderer', pid=game.pid, in_front=pad.bring_to_front(game.pid))
            bridge, _ = pad.prepare(game.pid, process, names=pad.BRIDGE_EXPORTS)
            call_with_payload(process, bridge['SpidyStart'],
                              struct.pack('<4I3Q', 0x53424346, 1, 40, game.pid, game.base, 0, 0))
        xr, digest = pad.prepare(game.pid, process, pad.XR_DLL, pad.ROOT/'reports/stereo-modules', EXPORTS)
        out['sha256'] = digest
        flags = FLAGS['web_grab'] | FLAGS['punch'] | FLAGS['body'] | FLAGS['air_webs'] | FLAGS['stand_on_walls']
        code = call_with_payload(process, xr['SpidyMenuStart'], settings_payload(flags, 30, 100, 1, 32.0))
        event('menu hooks', code=code)
        if code:
            return 1
        call_with_payload(process, xr['SpidyPromptsWatch'], struct.pack('<I', 1))
        # The table through the game's own lookups, once its texts are loaded.
        for _ in range(60):
            out['check'] = check(game, process, xr)
            if out['check'].get('english'):
                break
            time.sleep(2)
        event('check', **out['check'])
        call_with_payload(process, xr['SpidyVrTextsSet'], struct.pack('<I', 1))
        event('VR wording on')
        if a.watch:
            until = time.monotonic() + a.watch
            while time.monotonic() < until and pad.alive(game):
                collect()
                snap('watch')
                save()
                time.sleep(2)
            return 0
        last_press = [0.0]

        def player_or_press():
            if time.monotonic() - last_press[0] >= 2:
                last_press[0] = time.monotonic()
                pad.pad(process, xr, pad.BUTTONS['a'], 150)
            player, _ = pad.find_player(game, process, xr)
            return bool(player)

        if not pad.find_player(game, process, xr)[0]:
            if not watch_until(player_or_press, 'load', 240):
                event('no player within four minutes')
                return 1
            event('player')
            watch_until(lambda: False, 'roam', a.seconds)
        for n in range(a.restarts):
            # Pause, Restart last checkpoint (under RESUME and Spidy's SWITCH TO GAME SCREEN), Are you sure: yes.
            for button, wait in (('start', 1.5), ('down', .6), ('down', .6), ('a', 1.2), ('a', 1.0)):
                pad.pad(process, xr, pad.BUTTONS[button], 150)
                time.sleep(wait)
            event('restart', n=n + 1)
            gone = [False]

            def back():
                player, _ = pad.find_player(game, process, xr)
                gone[0] |= not player
                return gone[0] and bool(player)

            if not watch_until(back, f'restart{n + 1}', 120, tips=True):
                event('no player after the restart', n=n + 1)
                break
            watch_until(lambda: False, f'roam{n + 1}', a.seconds)
        save()
    finally:
        save()
        pad.close(process)
    summary = dict(died=out['died'], check=out.get('check'), vr_texts=out['vr_texts'],
                   show_texts=out['show_texts'], rewrites=out.get('watched', {}).get('rewrites'))
    print(json.dumps(summary, indent=1), flush=True)
    print(f'report {a.output}; screenshots {pad.OUTPUT}', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())

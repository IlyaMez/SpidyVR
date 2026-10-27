"""The VR settings a Linux session leaves for the next one, as Spidy Launcher.exe keeps them on Windows.

tools/spidy-linux.sh runs this with the host's Python 3, not the game's Wine one. run_game_vr.py takes the
settings the launcher offers only as arguments, and prints 'VR settings from the headset: ...' when a session
ends with others than it began with (the SPIDY VR tab, a T-pose calibration). `save` keeps that line's values;
`load` prints the arguments for them, one a line, for the next session. The values and their ranges are the
launcher's (launcher_text.hpp: headsetSettings, sessionArguments); defaults go unsaid. The tab's other rows the
game module keeps itself, in spidy_vr.ini.

    python3 tools/vr_settings_persist.py save "VR settings from the headset: aim_markers=1 ..."
    python3 tools/vr_settings_persist.py load
"""
import os
import pathlib
import sys

PREFIX = 'VR settings from the headset: '
# Switches: the setting, the value that needs saying, the argument that says it.
SWITCHES = (('aim_markers', 0, '--no-aim-markers'), ('air_webs', 0, '--no-air-webs'), ('flips', 1, '--flips'),
            ('trigger_webs', 1, '--trigger-webs'), ('arcade_webs', 1, '--arcade-webs'))
# Numbers: the setting, its range, its default and its argument.
NUMBERS = (('swing_speed', 10, 65, 32, '--swing-speed'), ('snap_turn', 0, 90, 30, '--snap-turn'),
           ('smooth_turn', 0, 360, 0, '--smooth-turn'), ('haptics', 0, 100, 100, '--haptics'),
           ('body_haptics', 0, 100, 100, '--body-haptics'), ('screen_size', 0, 2, 1, '--screen-size'),
           ('screen_depth', 0, 3, 2, '--screen-depth'), ('weight', 40, 300, 100, '--weight'),
           ('hud', 0, 3, 2, '--hud'), ('flip_speed', 90, 480, 180, '--flip-speed'))
# A T-pose calibration, millimetres (body_calibration.hpp): both within range, or none.
EYE_HEIGHT, ARM_LENGTH = (1000, 2500), (250, 1200)


def settings_file():
    config = os.environ.get('XDG_CONFIG_HOME') or os.path.join(os.path.expanduser('~'), '.config')
    return pathlib.Path(config) / 'spidy' / 'vr_settings.txt'


def parse(line):
    """The settings a 'VR settings from the headset' line (or the saved file) holds, as whole numbers."""
    if line.startswith(PREFIX):
        line = line[len(PREFIX):]
    values = {}
    for pair in line.split():
        key, equals, value = pair.partition('=')
        try:
            values[key] = int(value) if equals else None
        except ValueError:
            pass
    return {k: v for k, v in values.items() if v is not None}


def calibrated(values):
    eye, arm = values.get('eye_height_mm', 0), values.get('arm_length_mm', 0)
    return EYE_HEIGHT[0] <= eye <= EYE_HEIGHT[1] and ARM_LENGTH[0] <= arm <= ARM_LENGTH[1]


def arguments(values):
    """run_game_vr.py's arguments for `values`, as the launcher would start a session with them."""
    args = [flag for key, said, flag in SWITCHES if key in values and int(values[key] != 0) == said]
    if calibrated(values):
        args += ['--eye-height', str(values['eye_height_mm']), '--arm-length', str(values['arm_length_mm'])]
    elif values.get('calibration_prompt') == 0:
        args.append('--no-calibration-prompt')
    for key, low, high, default, flag in NUMBERS:
        if key in values and min(max(values[key], low), high) != default:
            args += [flag, str(min(max(values[key], low), high))]
    return args


def save(line, path):
    """Keeps the line's settings over the saved ones; a calibration half made or out of range keeps the saved
    one."""
    values = parse(path.read_text()) if path.exists() else {}
    new = parse(line)
    if not calibrated(new) and (new.get('eye_height_mm') or new.get('arm_length_mm')):
        new.pop('eye_height_mm', None)
        new.pop('arm_length_mm', None)
    values.update(new)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.tmp')
    temporary.write_text(' '.join(f'{k}={v}' for k, v in values.items()) + '\n')
    temporary.replace(path)


def main(argv):
    path = settings_file()
    if argv[1:] == ['load']:
        # One a line, and nothing at all for none: the script's mapfile makes an empty line an argument.
        for arg in arguments(parse(path.read_text())) if path.exists() else []:
            print(arg)
        return 0
    if argv[1:2] == ['save'] and len(argv) == 3:
        save(argv[2], path)
        return 0
    print('\n'.join(__doc__.strip().splitlines()[-2:]), file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))

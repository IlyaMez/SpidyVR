"""Summarize the native camera diagnostic without claiming stereo or movement support."""
import argparse
import json
import math
import pathlib
from analyze_camera_capture import distance, determinant


def angle_degrees(a, b):
    trace = sum(sum(x * y for x, y in zip(ra, rb)) for ra, rb in zip(a, b))
    return math.degrees(math.acos(max(-1., min(1., (trace - 1.) * .5))))


def summarize(data):
    samples = data['samples']
    valid = [s for s in samples if s.get('validation', 1) == 0 and s['qpc'] and s['thread']]
    result = dict(pid=data['pid'], started_utc=data['started_utc'],
                  sampled_updates=len(samples), valid_samples=len(valid),
                  hook_disabled=data['stopped'], camera_entry_restored=data['camera_entry_restored'],
                  native_stereo_verified=False,
                  note='This confirms callback observations in the tested scene only. '
                       'Render ownership, simulation frame boundaries, cutscenes, loading, '
                       'and the movement API still require separate validation.')
    if not valid: return result
    first, last = valid[0], valid[-1]
    duration = (last['qpc'] - first['qpc']) / first['frequency']
    result.update(
        callback_thread_ids=sorted({s['thread'] for s in valid}),
        observed_managers=sorted({s['manager'] for s in valid}),
        update_objects=sorted({s['update_object'] for s in valid}),
        update_vtable_rvas=sorted({hex(int(s['update_vtable'], 16) - int(data['module_base'], 16)) for s in valid}),
        actor_records=sorted({s['actor_record'] for s in valid}),
        observed_callback_seconds=duration,
        observed_callback_rate_hz=(last['calls'] - first['calls']) / duration if duration > 0 else None,
        callback_count_end=last['calls'], rejected_count_end=last['rejected'],
        overlapping_count_end=last['overlapping'],
        dt_range_seconds=[min(s['dt'] for s in valid), max(s['dt'] for s in valid)],
        flags_seen=sorted({s['flags'] for s in valid}),
        player_max_displacement=max(distance(s['player']['position'], first['player']['position']) for s in valid),
        camera_max_displacement=max(distance(s['camera']['position'], first['camera']['position']) for s in valid),
        camera_max_rotation_degrees=max(angle_degrees(s['camera']['basis'], first['camera']['basis']) for s in valid),
        camera_determinant_range=[min(determinant(s['camera']['basis']) for s in valid),
                                  max(determinant(s['camera']['basis']) for s in valid)])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    result = summarize(json.loads(args.capture.read_text(encoding='utf-8')))
    text = json.dumps(result, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + '\n', encoding='utf-8')
    print(text)


if __name__ == '__main__':
    main()

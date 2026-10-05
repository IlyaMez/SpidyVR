"""Summarize live camera candidates without treating polling as a verified game hook."""
import argparse
from collections import defaultdict
import json
import math
import pathlib


def distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def determinant(rows):
    a, b, c = rows
    return (a[0] * (b[1] * c[2] - b[2] * c[1])
            - a[1] * (b[0] * c[2] - b[2] * c[0])
            + a[2] * (b[0] * c[1] - b[1] * c[0]))


def summarize(capture):
    grouped = defaultdict(list)
    samples = capture.get('samples', [])
    for sample in samples:
        for item in sample['candidates']:
            grouped[(item['kind'], item['object'])].append(item)
    summaries = []
    for (kind, address), items in sorted(grouped.items()):
        if kind == 'follow_camera':
            summaries.append(dict(kind=kind, object=address, valid_samples=len(items),
                                  sample_fraction=len(items) / len(samples), actor_records=[],
                                  lens_ranges={field: [min(item['lens'][field] for item in items),
                                                       max(item['lens'][field] for item in items)]
                                               for field in ('fov_radians', 'near', 'far')}))
            continue
        player_positions = [item['player']['position'] for item in items]
        records = sorted({item['actor_record'] for item in items if 'actor_record' in item})
        result = dict(kind=kind, object=address, valid_samples=len(items),
                      sample_fraction=len(items) / len(samples), actor_records=records,
                      player_start=player_positions[0], player_end=player_positions[-1],
                      player_max_displacement=max(distance(p, player_positions[0]) for p in player_positions))
        cameras = [item for item in items if 'camera' in item]
        if cameras:
            separation = [distance(item['camera']['position'], item['player']['position']) for item in cameras]
            determinants = [determinant(item['camera']['basis']) for item in cameras]
            first = cameras[0]['camera']
            result['camera'] = dict(
                start=first['position'], end=cameras[-1]['camera']['position'],
                max_displacement=max(distance(item['camera']['position'], first['position']) for item in cameras),
                player_separation_min=min(separation), player_separation_max=max(separation),
                basis_determinant_min=min(determinants), basis_determinant_max=max(determinants),
                max_basis_change=max(distance(row, first['basis'][axis])
                                     for item in cameras for axis, row in enumerate(item['camera']['basis'])))
        summaries.append(result)
    record_owners = defaultdict(set)
    for item in summaries:
        for record in item['actor_records']:
            record_owners[record].add(item['kind'])
    shared = sorted(record for record, owners in record_owners.items()
                    if {'hero_local', 'hero_camera_manager'} <= owners)
    return dict(pid=capture['pid'], scan_complete=capture['scan_complete'],
                started_utc=capture.get('started_utc'),
                sampled_frames=len(samples), candidates=summaries, shared_actor_records=shared,
                runtime_abi_verified=False, native_stereo_verified=False,
                note='Shared actor records and stable transforms support object discovery only. '
                     'Camera ownership, thread timing, render passes, and coordinate conventions remain unverified.')


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

"""Reject torn or incompatible telemetry before it drives integration decisions."""
import math
import pathlib
import struct
import sys
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from probe_stereo_gpu import snapshot as gpu_snapshot, save_eye_images
from run_game_vr import snapshot as xr_snapshot, accepted as accepted_xr, timing_snapshot, frame_rates, appearance_snapshot
from probe_collision import snapshot as collision_snapshot
from probe_movement import snapshot as movement_snapshot
from probe_native_motion import snapshot as motion_snapshot
from capture_game_state import Game, LIVE_VTABLES
from probe_native_rays import snapshot as ray_snapshot, command as ray_command
from probe_game_swing import snapshot as swing_snapshot, command as swing_command, assess as assess_swing


class Reader:
    def __init__(self, *values):
        self.values = iter(values)

    def read(self, address, size):
        return next(self.values)


class ProtocolTests(unittest.TestCase):
    def test_appearance_decodes_each_eye_and_rejects_torn_reads(self):
        raw = bytearray(288)
        struct.pack_into('<4I6Q', raw, 0, 0x53415044, 4, 288, 1, 6, 11, 12, 21, 22, 0x123456789)
        struct.pack_into('<2f', raw, 280, -1, -1)
        struct.pack_into('<3Q2I4Q2f2I2Q2fd', raw, 64, 300, 2, 1, 0x4000002a, 0x120,
                         3, 4, 0xabc0, 0xdef0, .25, 1.5, 0x5000001, 0x100, 40, 2, .5, 1.25, 10.0)
        result = appearance_snapshot(Reader(raw, struct.pack('<Q', 6)), 0)
        self.assertEqual(result['hidden_avatar'], [11, 12])
        self.assertEqual(result['srgb_overlay'], [21, 22])
        self.assertEqual(result['player_actor'], '0x123456789')
        self.assertEqual((result['hero_state'], result['hero_handle'], result['hero_flags']),
                         ('valid', '0x4000002a', '0x120'))
        self.assertEqual((result['native_hidden_frames'], result['native_hides'], result['native_restores']),
                         (300, 2, 1))
        self.assertEqual((result['near_instances'], result['near_instance'], result['near_model']),
                         ([3, 4], '0xabc0', '0xdef0'))
        self.assertEqual((result['near_distance'], result['near_radius']), (.25, 1.5))
        self.assertEqual((result['anchored_frames'], result['anchor_rejected']), (40, 2))
        self.assertEqual((result['anchor_last_m'], result['anchor_max_m'], result['anchor_mean_m']), (.5, 1.25, .25))
        self.assertEqual((result['web_state'], result['web_live']), ('off', [False, False]))
        self.assertEqual((result['active_view_aligned'], result['active_view_rejected']), (0, 0))
        self.assertIsNone(result['active_view_fov_deg'])
        self.assertEqual((result['shared_hero_frames'], result['hero_lag_mean_m']), (0, 0.0))
        self.assertEqual((result['web_start_error_m'], result['web_start_error_max_m']), (None, None))
        struct.pack_into('<2I4Q', raw, 176, 2, 2, 5, 4, 1, 900)
        struct.pack_into('<2Q5f', raw, 216, 120, 3, -1, 1, -.5, 2, 3.5)
        result = appearance_snapshot(Reader(raw, struct.pack('<Q', 6)), 0)
        self.assertEqual((result['web_state'], result['web_live']), ('game_webs', [False, True]))
        self.assertEqual((result['web_creates'], result['web_releases'], result['web_failures'], result['web_updates']),
                         (5, 4, 1, 900))
        self.assertEqual((result['active_view_aligned'], result['active_view_rejected']), (120, 3))
        self.assertEqual(result['active_view_shift_m'], 3.5)
        struct.pack_into('<Q2fd', raw, 256, 8, .25, .75, 2.0)
        result = appearance_snapshot(Reader(raw, struct.pack('<Q', 6)), 0)
        self.assertEqual((result['shared_hero_frames'], result['hero_lag_last_m'], result['hero_lag_max_m'],
                          result['hero_lag_mean_m']), (8, .25, .75, .25))
        struct.pack_into('<2f', raw, 280, 0, .5)
        result = appearance_snapshot(Reader(raw, struct.pack('<Q', 6)), 0)
        self.assertEqual((result['web_start_error_m'], result['web_start_error_max_m']), (0, .5))
        horizontal, vertical = result['active_view_fov_deg']
        self.assertAlmostEqual(horizontal, 90, places=4)
        self.assertAlmostEqual(vertical, math.degrees(math.atan(2) + math.atan(.5)), places=4)
        self.assertIsNone(appearance_snapshot(Reader(*([raw, struct.pack('<Q', 8)]*8)), 0))
        self.assertIsNone(appearance_snapshot(Reader(raw[:64]), 0))
        struct.pack_into('<Q', raw, 16, 7)
        self.assertIsNone(appearance_snapshot(Reader(*([raw]*8)), 0))

    def test_frame_rates_do_not_count_reused_images_as_new_scene_frames(self):
        samples=[dict(status=2,seconds=0,submitted=0),
                 dict(status=3,seconds=10,submitted=100,unique_submitted=60,reused_submitted=40),
                 dict(status=3,seconds=12,submitted=300,unique_submitted=160,reused_submitted=140),
                 dict(status=3,seconds=14,submitted=300,unique_submitted=160,reused_submitted=140),
                 dict(status=4,seconds=15,submitted=300,unique_submitted=160,reused_submitted=140)]
        result=frame_rates(samples)
        self.assertEqual(result,dict(sample_seconds=2,submitted_fps=100,new_scene_fps=50,reused_fps=50))
        self.assertIsNone(frame_rates(samples[:2]))
        self.assertIsNone(frame_rates([samples[1],samples[1]]))
        for s in samples[1:4]:
            del s['unique_submitted']
        self.assertIsNone(frame_rates(samples)['new_scene_fps'])
        for s,n in zip(samples[1:4],(100,300,400)):
            s['frames']=n
        self.assertEqual(frame_rates(samples)['submitted_fps'],50)

    def test_timing_decodes_stage_means_and_rejects_torn_reads(self):
        raw=bytearray(344)
        struct.pack_into('<4IQ',raw,0,0x5358544d,1,344,0,6)
        for i in range(10):
            struct.pack_into('<Q3d',raw,24+32*i,4,8+i*4,10+i,1+i)
        result=timing_snapshot(Reader(raw,struct.pack('<Q',6)),0)
        self.assertEqual(result['copy_wait']['mean_ms'],7)
        self.assertEqual(result['display_period']['last_ms'],10)
        self.assertIsNone(timing_snapshot(Reader(*([raw,struct.pack('<Q',8)]*8)),0))

    def test_xr_success_requires_images_no_faults_and_restored_game_entries(self):
        result = dict(final=dict(status=4,error=0,submitted=90),
                      gpu=dict(status=2,error=0,pairs=90,fence=90,completed=90),
                      game_entries_restored=True,xr_stop=0,bridge_stop=0,samples=[],swing_samples=[])
        self.assertTrue(accepted_xr(result))
        for changed in (dict(final=dict(status=4,error=0,submitted=0)),dict(game_entries_restored=False),
                        dict(swing_samples=[dict(error=3002)]),dict(samples=[dict(error=1)]),
                        dict(motion_samples=[dict(error=1501)]),
                        dict(gpu=dict(status=2,error=0,pairs=90,fence=91,completed=90))):
            self.assertFalse(accepted_xr({**result,**changed}))

    def test_swing_stop_cannot_hide_an_earlier_simulation_fault(self):
        samples = [dict(source_step=i,owned=1,webs=[dict(attached=False)],error=0) for i in range(6)]
        final = dict(attaches=1,releases=1,controlled=30,error=0)
        self.assertTrue(assess_swing(samples,final,True,(0,0,0))['passed'])
        samples[-1]['error'] = 3002
        self.assertFalse(assess_swing(samples,final,True,(0,0,0))['passed'])

    def test_swing_feedback_and_two_anchor_boundaries(self):
        raw = bytearray(240)
        struct.pack_into('<4IQ9Q10f2I', raw, 0, 0x53574441,3,240,2,4,
                         *range(1,10), *range(10,19), .01,1,0)
        struct.pack_into('<2I5f',raw,144,1,0x3000123,1,2,3,40,50)
        struct.pack_into('<2I5f',raw,172,0,0xffffffff,4,5,6,70,80)
        struct.pack_into('<4I',raw,224,2,1,7,0)
        result = swing_snapshot(Reader(raw,struct.pack('<Q',4)),0)
        self.assertEqual(result['source_step'],9)
        self.assertEqual(result['position'],(10,11,12))
        self.assertEqual(result['requested'],(16,17,18))
        self.assertEqual(result['webs'][0]['anchor'],(1,2,3))
        self.assertEqual(result['webs'][1]['length'],70)
        self.assertEqual((result['takeoff_phase'],result['takeoff_attempts'],result['takeoff_timeouts']),(2,1,7))
        self.assertEqual(result['native_contact'],0)
        packet = swing_command(27,(1,2,3),(1,2,-7),True)
        self.assertEqual(len(packet),168)
        self.assertEqual(struct.unpack_from('<3I',packet),(0x5357434d,2,168))
        self.assertAlmostEqual(struct.unpack_from('<f',packet,152)[0],.01)
        self.assertGreater(struct.unpack_from('<Q',packet,160)[0],0)
        self.assertEqual(struct.unpack_from('<Q',packet,16)[0],27)
        self.assertEqual(struct.unpack_from('<4f',packet,44),(0,0,0,1))
        self.assertEqual(struct.unpack_from('<I2f',packet,72),(1,1,1))

    def test_ray_geometry_serial_and_body_generation_boundaries(self):
        raw = bytearray(784)
        struct.pack_into('<4IQ5Q4I', raw, 0, 0x53525944, 2, 784, 2, 4, 9, 10, 100, 8, 0xabc, 1, 0, 7, 0)
        struct.pack_into('<7fI7f7I', raw, 80, 10, 20, 30, 100, 0, -1, 0, 90,
                         10, 14, 30, .06, 0, 1, 0, 3, 0x30008f3, 1087, 1, 1, 0, 0xa0000104)
        result = ray_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['serial'], result['count'], result['thread']), (9, 1, 7))
        hit = result['hits'][0]
        self.assertEqual((hit['position'], hit['tag'], hit['body_id']), ((10, 14, 30), 90, 0x30008f3))
        self.assertTrue(hit['body_matches'])
        self.assertTrue(hit['fixed'])
        packet = ray_command(11, [((10, 20, 30), (0, -1, 0), 100, 91)])
        self.assertEqual(len(packet), 288)
        self.assertEqual(struct.unpack_from('<Q', packet, 16)[0], 11)
        self.assertEqual(struct.unpack_from('<I', packet, 60)[0], 91)

    def test_eye_capture_rejects_unfinished_or_torn_gpu_results(self):
        final = dict(status=2, error=0, pairs=1, fence=2, completed=1,
                     width=64, height=64, left_pixels=0x10000, right_pixels=0x20000)
        game = Mock()
        with self.assertRaisesRegex(RuntimeError, 'did not complete'):
            save_eye_images(game, final, pathlib.Path('eyes.json'))
        game.read.assert_not_called()
        final['completed'] = 2
        game.read.return_value = b'partial'
        with patch('probe_stereo_gpu.write_png') as write:
            with self.assertRaisesRegex(RuntimeError, 'Incomplete'):
                save_eye_images(game, final, pathlib.Path('eyes.json'))
            write.assert_not_called()

    def test_gpu_stamps_and_eye_states_have_correct_boundaries(self):
        raw = bytearray(208)
        struct.pack_into('<4IQ', raw, 0, 0x53475044, 2, 208, 2, 4)
        struct.pack_into('<16Q', raw, 32, *range(1, 17))
        struct.pack_into('<6I', raw, 160, 8, 4, 1, 0, 512, 384)
        struct.pack_into('<3Q',raw,184,101,202,303)
        result = gpu_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['serial'], result['generation'], result['fence'], result['completed']), (5, 6, 7, 8))
        self.assertEqual((result['left_resource'], result['right_resource'], result['left_state'], result['right_state']),
                         (13, 14, 8, 4))
        self.assertEqual((result['width'], result['height']), (512, 384))
        self.assertEqual((result['captured'],result['reused'],result['captured_serial']),(101,202,303))

    def test_all_readers_reject_incompatible_dlls(self):
        for reader, magic, size in ((gpu_snapshot, 0x53475044, 208), (xr_snapshot, 0x53585244, 576),
                                     (collision_snapshot, 0x53435044, 2160),
                                     (movement_snapshot, 0x534d5044, 57408),
                                     (motion_snapshot, 0x534d5644, 176), (ray_snapshot, 0x53525944, 784),
                                     (swing_snapshot,0x53574441,240),(timing_snapshot,0x5358544d,344),
                                     (appearance_snapshot,0x53415044,288)):
            raw = bytearray(size)
            struct.pack_into('<4IQ', raw, 0, magic, 99, size, 2, 4)
            with self.assertRaisesRegex(RuntimeError, 'protocol mismatch'):
                reader(Reader(raw, struct.pack('<Q', 4)), 0)

    def test_collision_hit_and_caller_boundaries(self):
        raw = bytearray(2160)
        struct.pack_into('<4IQ', raw, 0, 0x53435044, 2, 2160, 2, 8)
        struct.pack_into('<13f5I', raw, 64, 10, 20, 30, 0, -1, 0, .06,
                         0, 1, 0, 10, 14, 30, 8, 0x30008f3, 1087, 0x410, 0)
        struct.pack_into('<3Q2I', raw, 1648, 0x118180fe, 0x300000, 2146, 0x410, 7)
        reader = Reader(raw, struct.pack('<Q', 8))
        reader.base = 0x10000000
        result = collision_snapshot(reader, 0)
        self.assertEqual(result['samples'][0]['position'], (10, 14, 30))
        self.assertEqual(result['samples'][0]['body_id'], 0x30008f3)
        self.assertEqual(result['callers'][0]['caller_rva'], '0x18180fe')
        self.assertEqual(result['callers'][0]['count'], 2146)

    def test_movement_rejects_sample_count_beyond_buffer(self):
        raw = bytearray(57408)
        struct.pack_into('<4IQ', raw, 0, 0x534d5044, 2, 57408, 0, 2)
        struct.pack_into('<I', raw, 56, 513)
        with self.assertRaisesRegex(RuntimeError, 'sample overflow'):
            movement_snapshot(Reader(raw, struct.pack('<Q', 2)), 0)

    def test_movement_decodes_request_result_and_disable_flag(self):
        raw = bytearray(57408)
        struct.pack_into('<4IQ', raw, 0, 0x534d5044, 2, 57408, 0, 2)
        struct.pack_into('<I', raw, 56, 1)
        struct.pack_into('<3Q12f2I3f3IQ', raw, 64, 1, 0x10ad4215, 3, *range(12),
                         13, 14, 20, 30, 40, 0x80000000, 0x2060010, 1, 0x123456789)
        reader = Reader(raw, struct.pack('<Q', 2))
        reader.base = 0x10000000
        sample = movement_snapshot(reader, 0)['samples'][0]
        self.assertEqual(sample['requested'], (20, 30, 40))
        self.assertEqual(sample['mover_flags'], 0x80000000)
        self.assertEqual(sample['result'], '0x123456789')
        self.assertEqual(sample['caller_rva'], '0xad4215')

    def test_native_motion_result_and_lease_status_boundaries(self):
        raw = bytearray(176)
        struct.pack_into('<4IQ5Q10f4I', raw, 0, 0x534d5644, 4, 176, 2, 4,
                         100, 200, 24, 1, 24, 10, 20, 30, 11, 20, 30, .5, 0, 0, .01,
                         0x80a0, 0x2060010, 7, 0)
        struct.pack_into('<3Q6f', raw, 120, 90, 23, 0x138c1340, .5, 0, 0, -.15, 30, .01)
        struct.pack_into('<2I',raw,168,0,2)
        result = motion_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual(result['controlled'], 24)
        self.assertEqual(result['gravity_corrections'], 24)
        self.assertEqual(result['requested'], (11, 20, 30))
        self.assertEqual(result['velocity'], (.5, 0, 0))
        self.assertEqual((result['grounded'],result['contact']),(False,2))
        self.assertEqual((result['status'], result['serial']), (2, 1))
        self.assertEqual((result['air_events'], result['air_overrides'], result['air_state']), (90, 23, 0x138c1340))
        self.assertEqual(result['air_velocity'], (.5, 0, 0))
        self.assertAlmostEqual(result['air_vertical'], -.15)
        self.assertIsNone(motion_snapshot(Reader(*([raw, struct.pack('<Q', 6)]*8)), 0))

    def registry(self, slot_generation=5, header_generation=5, changed=False):
        game = Game.__new__(Game)
        game.base = 0x10000000
        table, hero = 0x20000000, 0x30000000
        slots = bytearray(32)
        struct.pack_into('<QI', slots, 16, hero, slot_generation)
        header = bytearray(24)
        struct.pack_into('<Q', header, 0, game.base+LIVE_VTABLES['hero_local'])
        struct.pack_into('<I', header, 20, header_generation << 20 | 1)
        memory = {(game.base+0x7a44320, 8): struct.pack('<Q', table),
                  (game.base+0x7a44340, 4): struct.pack('<i', 2),
                  (table, 32): slots, (hero, 24): header,
                  (table+16, 12): struct.pack('<QI', hero, slot_generation+int(changed))}
        game.read = lambda address, size: memory.get((address, size), b'')
        game.candidate = Mock(return_value={'kind': 'hero_local', 'object': hex(hero)})
        return game

    def test_registry_accepts_matching_object_and_slot_generation(self):
        game = self.registry()
        self.assertEqual(len(game.registered_candidates()), 1)

    def test_registry_rejects_reused_slot_and_stale_object(self):
        for game in (self.registry(changed=True), self.registry(header_generation=4)):
            self.assertEqual(game.registered_candidates(), [])
            game.candidate.assert_not_called()

    def test_xr_never_publishes_partly_updated_pose(self):
        raw = bytearray(576)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 3, 576, 3, 4)
        self.assertIsNone(xr_snapshot(Reader(*([raw, struct.pack('<Q', 6)]*8)), 0))

    def test_xr_decodes_both_hands_and_status_message(self):
        raw = bytearray(576)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 3, 576, 3, 4)
        struct.pack_into('<16f', raw, 160, *range(16))
        struct.pack_into('<16f', raw, 224, *range(16, 32))
        raw[288:295] = b'Tracked'
        struct.pack_into('<2Q',raw,544,50,90)
        struct.pack_into('<4I',raw,560,1,7,2688,2784)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['hands'][0][12], result['hands'][1][12]), (12, 28))
        self.assertEqual(result['message'], 'Tracked')
        self.assertEqual((result['unique_submitted'],result['reused_submitted']),(50,90))
        self.assertEqual((result['flat_screen'],result['toggles'],result['eye_width'],result['eye_height']),(1,7,2688,2784))


if __name__ == '__main__':
    unittest.main()

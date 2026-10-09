"""Reject torn or incompatible telemetry before it drives integration decisions."""
import math
import pathlib
import struct
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, Mock, patch
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from probe_stereo_gpu import snapshot as gpu_snapshot, save_eye_images
from run_game_vr import (snapshot as xr_snapshot, accepted as accepted_xr, timing_snapshot, frame_rates,
                         appearance_snapshot, eye_snapshot, save_eye_snapshot, rgb_rows, crop_origin,
                         game_memory, keep_game_log, commit_warning, VR_COMMIT_MB, body_snapshot,
                         punch_snapshot, shooter_snapshot, start_settings, settings_line, SETTINGS_LINE,
                         scaled_eye_size, vr_commit_mb, rendering_line, EYE_COMMIT_BYTES,
                         slow_motion_snapshot, hud_snapshot, GAME_HOOKS)
from probe_collision import snapshot as collision_snapshot
from probe_movement import snapshot as movement_snapshot
from probe_native_motion import snapshot as motion_snapshot
from capture_game_state import Game, LIVE_VTABLES
from probe_native_rays import snapshot as ray_snapshot, command as ray_command
from probe_game_swing import (snapshot as swing_snapshot, command as swing_command, assess as assess_swing,
                              flight_summary)
from probe_game_grab import grab_snapshot, hand_command as grab_hand_command


class Reader:
    def __init__(self, *values):
        self.values = iter(values)

    def read(self, address, size):
        return next(self.values)


class ProtocolTests(unittest.TestCase):
    def test_appearance_decodes_each_eye_and_rejects_torn_reads(self):
        raw = bytearray(328)
        struct.pack_into('<4I6Q', raw, 0, 0x53415044, 5, 328, 1, 6, 11, 12, 21, 22, 0x123456789)
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
        self.assertEqual((result['web_hand_gap_frames'], result['web_hand_gap_mean_m']), (0, 0.0))
        self.assertEqual(result['active_view_lag_frames'], 0)
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
        struct.pack_into('<Q2fdQ2f', raw, 288, 4, .125, .75, 1.0, 3, .5, .625)
        result = appearance_snapshot(Reader(raw, struct.pack('<Q', 6)), 0)
        self.assertEqual((result['web_hand_gap_frames'], result['web_hand_gap_last_m'], result['web_hand_gap_max_m'],
                          result['web_hand_gap_mean_m']), (4, .125, .75, .25))
        self.assertEqual((result['active_view_lag_frames'], result['active_view_lag_last_m'],
                          result['active_view_lag_max_m']), (3, .5, .625))
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

    def test_grab_feedback_decodes_each_hand_the_body_driver_and_rejects_torn_reads(self):
        raw = bytearray(368)
        struct.pack_into('<4Iq10Q4I', raw, 0, 0x53475244, 3, 368, 2, 6, *range(1, 11), 5, 0, 6, 0)
        # Hands (48 bytes): phase, kind, target, end, length, taut, tension, trailing, reserved.
        struct.pack_into('<2IQ4fIf2I', raw, 120, 3, 1, 0x2156b32d740, -293.5, 2.25, -179.5, .7, 1, .125, 0, 0)
        struct.pack_into('<2IQ4fIf2I', raw, 168, 0, 1, 0x2156b32d800, 1.5, 2.5, 3.5, 0, 0, 0, 1, 0)
        struct.pack_into('<4f9Q', raw, 216, 6.5, 2.5, -13.25, 8.0, *range(20, 29))
        struct.pack_into('<6f2f', raw, 304, 1, 2, 3, 4, 5, 6, .00415, .0332)
        # Three thugs knocked into the game's flight, 40 steps steered, 2 blows for what they struck, 1 thug struck.
        struct.pack_into('<4Q', raw, 336, 3, 40, 2, 1)
        result = grab_snapshot(Reader(raw, struct.pack('<q', 6)), 0)
        self.assertEqual((result['grabs'], result['throws'], result['lost'], result['landed']), (3, 6, 8, 10))
        self.assertEqual((result['candidates'], result['kinds']), (5, 6))
        self.assertEqual((result['hands'][0]['phase'], result['hands'][0]['kind']), ('held', 1))
        self.assertEqual(result['hands'][0]['target'], '0x2156b32d740')
        self.assertEqual(result['hands'][0]['end'], (-293.5, 2.25, -179.5))
        self.assertEqual((result['hands'][0]['taut'], result['hands'][0]['tension'], result['hands'][0]['trailing']),
                         (1, .125, 0))
        # A web let go of, trailing the prop it threw.
        self.assertEqual((result['hands'][1]['phase'], result['hands'][1]['trailing']), ('none', 1))
        self.assertEqual(result['hands'][1]['end'], (1.5, 2.5, 3.5))
        self.assertEqual((result['last_throw'], result['time_scale']), ((6.5, 2.5, -13.25), 8.0))
        self.assertEqual((result['body_steps'], result['frees'], result['expired']), (20, 21, 28))
        self.assertEqual((result['commanded'], result['observed']), ((1, 2, 3), (4, 5, 6)))
        self.assertAlmostEqual(result['step_dt'], .0332, places=6)
        self.assertEqual((result['launches'], result['flown'], result['impacts'], result['struck']), (3, 40, 2, 1))
        torn = bytearray(raw)
        struct.pack_into('<q', torn, 16, 7)
        self.assertIsNone(grab_snapshot(Reader(*([torn, struct.pack('<q', 7)]*8)), 0))
        packet = grab_hand_command(9, (1, 2, 3), (0, 0, -1), (0, 0, .1), trigger=.25, grip=1)
        self.assertEqual(len(packet), 168)
        self.assertEqual(struct.unpack_from('<3I', packet), (0x5357434d, 2, 168))
        self.assertEqual(struct.unpack_from('<3f', packet, 60), (0, 0, struct.unpack('<f', struct.pack('<f', .1))[0]))
        self.assertEqual(struct.unpack_from('<I2f', packet, 72), (1, .25, 1))

    def test_body_status_decodes_errors_and_turn_and_rejects_torn_reads(self):
        raw = bytearray(144)
        struct.pack_into('<4Iq3Q2I2Q3fI2ff2fIQ2IdfI', raw, 0, 0x53424453, 2, 144, 2, 6, 900, 300, 290, 0, 237,
                         0x1a0, 0x2b0, 1., 1.03125, -.5, 1, .002, .003, .0005, .0125, .0375, 12, 480, 1, 2, 7.5,
                         1.0625, 1)
        result = body_snapshot(Reader(raw, struct.pack('<q', 6)), 0)
        self.assertEqual((result['state'], result['problem'], result['joints']), ('active', None, 237))
        self.assertEqual((result['hero_jobs'], result['solved'], result['renders']), (300, 290, 480))
        self.assertEqual((result['weight'], result['scale'], result['grounded']), (1., 1.0312, True))
        self.assertEqual((result['hand_error_m'], result['head_error_m']), ([.002, .003], .0005))
        self.assertEqual((result['turn_last'], result['turn_max'], result['solve_ms']), (.0125, .0375, 7.5))
        self.assertEqual((result['rig'], result['rig_switches'], result['hero_jobs_max']), ('0x1a0', 12, 2))
        # The arms 6% longer than the body's scale makes them, both sized by the player's T-pose calibration.
        self.assertEqual((result['arm_scale'], result['calibrated']), (1.0625, True))
        struct.pack_into('<2I', raw, 48, 3, 237)
        self.assertEqual(body_snapshot(Reader(raw, struct.pack('<q', 6)), 0)['problem'], 'unknown_rig')
        torn = bytearray(raw)
        struct.pack_into('<q', torn, 16, 7)
        self.assertIsNone(body_snapshot(Reader(*([torn, struct.pack('<q', 7)]*8)), 0))

    def test_punch_feedback_decodes_each_hand_and_the_latest_blow(self):
        raw = bytearray(160)
        struct.pack_into('<4Iq4Q2I', raw, 0, 0x53505544, 1, 160, 2, 4, 5000, 3, 3, 1, 2, 0)
        # Hands (32 bytes): punches, last target, last strength, speed, last knockback, busy.
        struct.pack_into('<2Q2f2I', raw, 64, 1, 0x1c517822940, .25, .5, 2, 0)
        struct.pack_into('<2Q2f2I', raw, 96, 2, 0x1c517822a00, 1., 9.5, 5, 1)
        struct.pack_into('<3f3f2f', raw, 128, 1.5, 95.25, 2393.75, 0, 0, -1, 40, 8.25)
        result = punch_snapshot(Reader(raw, struct.pack('<q', 4)), 0)
        self.assertEqual((result['punches'], result['issued'], result['dropped'], result['bots']), (3, 3, 1, 2))
        self.assertEqual((result['hands'][0]['punches'], result['hands'][0]['last_knockback']), (1, 2))
        self.assertEqual(result['hands'][1]['last_target'], '0x1c517822a00')
        self.assertEqual((result['hands'][1]['speed'], result['hands'][1]['busy']), (9.5, True))
        self.assertEqual((result['last_point'], result['last_direction']), ([1.5, 95.25, 2393.75], [0, 0, -1]))
        self.assertEqual((result['last_damage'], result['last_speed']), (40, 8.25))
        torn = bytearray(raw)
        struct.pack_into('<q', torn, 16, 5)
        self.assertIsNone(punch_snapshot(Reader(*([torn, struct.pack('<q', 5)]*8)), 0))

    def test_slow_motion_decodes_presses_focus_and_the_games_time_and_rejects_torn_reads(self):
        raw = bytearray(96)
        struct.pack_into('<4Iq6I', raw, 0, 0x574f4c53, 1, 96, 0, 6, 3, 2, 1, 1, 0, 1)
        struct.pack_into('<7fI2Q', raw, 48, 4.5, .25, 1, .3, 1, .3, .01, 1, 9000, 1200)
        result = slow_motion_snapshot(Reader(raw, struct.pack('<q', 6)), 0)
        self.assertEqual((result['status'], result['starts'], result['stops'], result['refused'],
                          result['emptied'], result['interrupted'], result['active']), (0, 3, 2, 1, 1, 0, True))
        self.assertEqual((result['seconds'], result['focus'], result['blend']), (4.5, .25, 1))
        self.assertAlmostEqual(result['wanted'], .3, 6)
        self.assertAlmostEqual(result['world_scale'], .3, 6)
        self.assertAlmostEqual(result['physics_step'], .01, 6)
        self.assertEqual((result['game_scale'], result['physics_scaled']), (1, True))
        self.assertEqual((result['updates'], result['slowed_updates']), (9000, 1200))
        torn = bytearray(raw)
        struct.pack_into('<q', torn, 16, 7)
        self.assertIsNone(slow_motion_snapshot(Reader(*([torn, struct.pack('<q', 7)]*8)), 0))
        wrong = bytearray(raw)
        struct.pack_into('<I', wrong, 4, 2)
        with self.assertRaises(RuntimeError):
            slow_motion_snapshot(Reader(wrong, struct.pack('<q', 6)), 0)
        # The session checks the time system's update is unpatched before and restored after.
        self.assertIn(0x19bb430, GAME_HOOKS)

    def test_hud_decodes_the_panel_its_texture_and_the_second_movie_and_rejects_torn_reads(self):
        raw = bytearray(192)
        struct.pack_into('<4Iq2Q3Q', raw, 0, 0x53485544, 3, 192, 0, 6, 0x1000, 0x2000, 2930, 2494, 3)
        struct.pack_into('<3f5f', raw, 64, 14.4446, 10.682, 11, 20, .8125, 2, .577, 0)
        struct.pack_into('<4I4Q2I', raw, 96, 1920, 1080, 1920, 1080, 0, 0, 2840, 4988, 0, 0)
        # The large HUD; none left out; 2400 placements where the XR worker's follow turned the panel, which
        # last faced 1.5 degrees from the head and at most 31.25.
        struct.pack_into('<2I3Q2f', raw, 152, 3, 0, 0, 0, 2400, 1.5, 31.25)
        result = hud_snapshot(Reader(raw, struct.pack('<q', 6)), 0)
        self.assertEqual((result['game_frames'], result['placed_frames'], result['rejected_frames']), (2930, 2494, 3))
        self.assertEqual((result['game_distance'], result['view_half_width'], result['distance']), (20, .8125, 2))
        self.assertEqual((result['texture'], result['game_texture']), ([1920, 1080], [1920, 1080]))
        self.assertEqual((result['texture_resizes'], result['texture_restores'], result['layer_frames'],
                          result['marker_projections']), (0, 0, 2840, 4988))
        self.assertEqual((result['layer_status'], result['restored']), (0, False))
        self.assertEqual((result['size'], result['hidden_frames'], result['hidden_draws'], result['followed_frames'],
                          result['follow_angle'], result['follow_angle_max']), (3, 0, 0, 2400, 1.5, 31.25))
        # The HUD off: 300 frames left the panel out of both eyes.
        struct.pack_into('<2I3Q', raw, 152, 0, 0, 300, 600, 2400)
        result = hud_snapshot(Reader(raw, struct.pack('<q', 6)), 0)
        self.assertEqual((result['size'], result['hidden_frames'], result['hidden_draws']), (0, 300, 600))
        torn = bytearray(raw)
        struct.pack_into('<q', torn, 16, 7)
        self.assertIsNone(hud_snapshot(Reader(*([torn, struct.pack('<q', 7)]*8)), 0))
        wrong = bytearray(raw)
        struct.pack_into('<I', wrong, 4, 1)
        with self.assertRaises(RuntimeError):
            hud_snapshot(Reader(wrong, struct.pack('<q', 6)), 0)
        # The session checks the HUD's hooked entries are unpatched before and restored after.
        for rva in (0x73ab80, 0x2104400, 0x1d2b6c0, 0x1f10ad0, 0x1f10b60):
            self.assertIn(rva, GAME_HOOKS)

    def test_shooter_feedback_decodes_each_hand_and_the_latest_shot(self):
        raw = bytearray(192)
        # 900 samples, 5 pulls, 4 shots fired, 1 dropped, 2 aimed at a thug, 1 target taken; the gadget, 4000
        # frames, 3 thugs on offer.
        struct.pack_into('<4Iq8Q2I', raw, 0, 0x53484f44, 2, 192, 2, 4, 900, 5, 4, 1, 2, 1, 0x1c5dd1c0560, 4000, 3, 0)
        struct.pack_into('<2Q', raw, 96, 1, 0)
        struct.pack_into('<2Q', raw, 112, 3, 0x1c517822a00)
        struct.pack_into('<3f3fQ', raw, 128, 1.5, 95.25, 2393.75, 20, 96, 2390, 0x1c5dd3a0540)
        # Three shots struck something: two webbed a thug, one knocked a prop; the latest hit a thug.
        struct.pack_into('<4Q', raw, 160, 3, 2, 1, 0x1c517822a00)
        result = shooter_snapshot(Reader(raw, struct.pack('<q', 4)), 0)
        self.assertEqual((result['requested'], result['fired'], result['dropped'], result['targeted'],
                          result['resolved']), (5, 4, 1, 2, 1))
        self.assertEqual((result['weapon'], result['frames'], result['bots']), ('0x1c5dd1c0560', 4000, 3))
        self.assertEqual([h['shots'] for h in result['hands']], [1, 3])
        self.assertEqual(result['hands'][1]['last_target'], '0x1c517822a00')
        self.assertEqual((result['last_origin'], result['last_aim_point']), ([1.5, 95.25, 2393.75], [20, 96, 2390]))
        self.assertEqual(result['last_shot'], '0x1c5dd3a0540')
        self.assertEqual((result['collisions'], result['webbed'], result['pushed'], result['last_hit']),
                         (3, 2, 1, '0x1c517822a00'))
        torn = bytearray(raw)
        struct.pack_into('<q', torn, 16, 5)
        self.assertIsNone(shooter_snapshot(Reader(*([torn, struct.pack('<q', 5)]*8)), 0))

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

    def eye_header(self, count=3, pixels=0x50000, width=64, height=64, pitch=256, sequence=8):
        raw = bytearray(112)
        struct.pack_into('<4IQ4Q4I2f8f', raw, 0, 0x53455353, 1, 112, 2, sequence, count, pixels, 77, 99,
                         width, height, pitch, 0, 31.5, .25, 10, 20, -1, -1, -1, -1, 40, 44)
        return raw

    def test_eye_snapshot_header_decodes_hands_and_rejects_torn_reads(self):
        raw = self.eye_header()
        shot = eye_snapshot(Reader(raw, struct.pack('<Q', 8)), 0)
        self.assertEqual((shot['count'], shot['pixels'], shot['generation'], shot['serial']), (3, 0x50000, 77, 99))
        self.assertEqual((shot['width'], shot['height'], shot['row_pitch'], shot['flat_screen']), (64, 64, 256, False))
        self.assertEqual((shot['speed_mps'], shot['web_hand_gap_m']), (31.5, .25))
        self.assertEqual(shot['game_web'], [False, True])
        self.assertEqual(shot['wrist_px'], [[10, 20], None])
        self.assertEqual(shot['rope_start_px'], [None, [40, 44]])
        self.assertIsNone(eye_snapshot(Reader(*([raw, struct.pack('<Q', 10)]*8)), 0))
        self.assertIsNone(eye_snapshot(Reader(raw[:50]), 0))
        struct.pack_into('<f', raw, 76, -1)
        self.assertIsNone(eye_snapshot(Reader(raw, struct.pack('<Q', 8)), 0)['web_hand_gap_m'])

    def test_eye_rows_keep_colour_order_through_reduction_and_cropping(self):
        width, height, pitch = 8, 4, 40  # padded rows, as GPU readbacks have
        data = bytearray(pitch*height)
        for y in range(height):
            for x in range(width):
                data[y*pitch+x*4:y*pitch+x*4+4] = bytes((x, y, x+16*y, 255))
        self.assertEqual(rgb_rows(bytes(data), pitch, 0, 0, width, height), (8, 4, b''.join(
            bytes((x, y, x+16*y)) for y in range(height) for x in range(width))))
        self.assertEqual(rgb_rows(bytes(data), pitch, 0, 0, width, height, 3), (3, 2, b''.join(
            bytes((x, y, x+16*y)) for y in (0, 3) for x in (0, 3, 6))))
        self.assertEqual(rgb_rows(bytes(data), pitch, 5, 1, 2, 2), (2, 2, b''.join(
            bytes((x, y, x+16*y)) for y in (1, 2) for x in (5, 6))))
        self.assertEqual([crop_origin(c, 512, 3072) for c in (-40, 100, 1536, 3000, 9000)],
                         [0, 0, 1280, 2560, 2560])

    def test_eye_snapshot_saves_view_and_hand_crop_and_drops_replaced_copies(self):
        raw = self.eye_header()
        shot = eye_snapshot(Reader(raw, struct.pack('<Q', 8)), 0)
        pixels = bytes(256*63+64*4)
        folder = MagicMock()
        with patch('run_game_vr.write_rgb_png') as write:
            game = Reader(pixels, raw, struct.pack('<Q', 8))
            files = save_eye_snapshot(game, 0, shot, folder, '0003', view=32, crop=16)
            self.assertEqual(files, dict(view='0003-view.png', view_scale=2, right_hand='0003-right-hand.png',
                                         right_hand_origin=[32, 36]))
            self.assertEqual([call.args[1:3] for call in write.call_args_list], [(32, 32), (16, 16)])
            write.reset_mock()
            # A newer copy replaced the pixels while they were read.
            newer = self.eye_header(count=4, sequence=10)
            self.assertIsNone(save_eye_snapshot(Reader(pixels, newer, struct.pack('<Q', 10)), 0, shot, folder, '0003'))
            self.assertIsNone(save_eye_snapshot(Reader(pixels[:100], raw, struct.pack('<Q', 8)), 0, shot, folder, '0003'))
            shot['pixels'] = 0
            self.assertIsNone(save_eye_snapshot(Reader(), 0, shot, folder, '0003'))
            write.assert_not_called()

    def test_game_log_is_kept_beside_the_report_with_its_memory_lines(self):
        line = ('13:33:01:870 (00009916) > [Render] Working set: 3170MB Page file: 10144MB Video Budget: 31418MB '
                'Video Usage: 3296MB Sys Budget: 45929MB Sys Usage: 379MB Tex Usage: 974MB Tex Budget: 3874MB '
                'BVH heaps: 60MB BVH size: 55MB Demoted: 12MB fps: 43.5')
        log = '13:31:58:000 (1) > [Startup] Build: v4.630.0.0\n'+line+'\n13:34:01:870 (2) > [Render] Working set: 31\n'
        rows = game_memory(log)
        self.assertEqual(rows, [dict(time='13:33:01', working_set_mb=3170, commit_mb=10144, video_budget_mb=31418,
                                     video_usage_mb=3296, texture_usage_mb=974, texture_budget_mb=3874,
                                     demoted_mb=12, fps=43.5)])
        with tempfile.TemporaryDirectory() as folder:
            folder = pathlib.Path(folder)
            (folder/'game.log').write_text(log, encoding='utf-8')
            copy, memory = keep_game_log(folder/'game-vr-1.json', folder/'game.log')
            self.assertEqual(copy, folder/'game-vr-1-game.log')
            self.assertEqual(copy.read_text(encoding='utf-8'), log)
            self.assertEqual(memory, rows)
            # A session without a readable game log still gets its report.
            self.assertEqual(keep_game_log(folder/'game-vr-2.json', folder/'missing.log'), (None, []))
            self.assertFalse((folder/'game-vr-2-game.log').exists())

    def test_memory_warning_only_when_windows_cannot_promise_a_vr_session(self):
        self.assertIsNone(commit_warning(None))  # unknown: nothing to say
        self.assertIsNone(commit_warning(VR_COMMIT_MB))
        # October 5: 14.5 GB left with the game closed.
        text = commit_warning(14848)
        self.assertRegex(text, r'^WARNING: Windows can promise programs only 14\.5 GB more memory, and the game in '
                               r'VR takes about 19 GB\. Close large programs')
        self.assertIn('page file', text)
        # A game that is already running holds part of the total: 14.6 GB after loading, measured.
        self.assertIsNone(commit_warning(VR_COMMIT_MB-14557, 14557))
        self.assertIn('only 4.3 GB more memory, and VR takes about 4 GB on top of the running game. Close',
                      commit_warning(VR_COMMIT_MB-14558, 14557))
        self.assertIsNone(commit_warning(4096, needed_mb=4096))
        self.assertIsNotNone(commit_warning(4095, needed_mb=4096))
        # A game that already holds more than a session takes needs nothing more.
        self.assertIsNone(commit_warning(0, VR_COMMIT_MB+1))
        # The running game's share is read from Windows; this process stands in for the game.
        import os
        import run_game_vr
        with patch.object(run_game_vr, 'find_game', return_value=os.getpid()):
            self.assertTrue(4 < run_game_vr.game_commit_mb() < 4096)
        with patch.object(run_game_vr, 'find_game', side_effect=RuntimeError('Spider-Man is not running.')):
            self.assertIsNone(run_game_vr.game_commit_mb())
        self.assertIsNone(run_game_vr.process_commit_mb(0))  # no such process to ask
        # At a console the person there decides; an unattended run only prints.
        ask = Mock()
        with patch('builtins.print') as shown:
            run_game_vr.announce_low_memory(None, True, ask)
            run_game_vr.announce_low_memory(text, False, ask)
            ask.assert_not_called()
            shown.assert_called_once_with(text, flush=True)
            run_game_vr.announce_low_memory(text, True, ask)
            ask.assert_called_once()
            run_game_vr.announce_low_memory(text, True, Mock(side_effect=EOFError))
            with self.assertRaises(KeyboardInterrupt):  # cancels the launch
                run_game_vr.announce_low_memory(text, True, Mock(side_effect=KeyboardInterrupt))

    def test_the_render_scale_sizes_the_eyes_and_the_memory_they_take(self):
        # core_tests.cpp's cases for spidy::scaledEyeSize: the console and the warning count as the DLL does.
        self.assertEqual(scaled_eye_size(3072, 3264, 100), (3072, 3264))
        self.assertEqual(scaled_eye_size(2500, 2690, 100), (2500, 2690))
        self.assertEqual(scaled_eye_size(3072, 3264, 150), (4608, 4896))
        self.assertEqual(scaled_eye_size(2496, 2688, 125), (3120, 3360))
        self.assertEqual(scaled_eye_size(2496, 2688, 50), (1248, 1344))
        self.assertEqual(scaled_eye_size(2500, 2690, 110), (2752, 2960))
        self.assertEqual(scaled_eye_size(4032, 3648, 200), (8064, 7296))
        self.assertEqual(scaled_eye_size(4320, 4320, 200), (8192, 8192))
        width, height = scaled_eye_size(3072, 3264, 200, 5000)
        self.assertEqual((width % 8, height), (0, 5000))
        self.assertAlmostEqual(width/height, 3072/3264, delta=.003)
        # Eyes larger than VR_COMMIT_MB's 3072 x 3264 need more memory; smaller ones no less.
        self.assertEqual(vr_commit_mb([3072, 3264]), VR_COMMIT_MB)
        self.assertEqual(vr_commit_mb([2496, 2688]), VR_COMMIT_MB)
        self.assertEqual(vr_commit_mb(None, 150), VR_COMMIT_MB)
        self.assertEqual(vr_commit_mb([3072, 3264], 150), VR_COMMIT_MB+(2*(4608*4896-3072*3264)*EYE_COMMIT_BYTES >> 20))
        self.assertEqual(vr_commit_mb([2496, 2688], 100, 4096), VR_COMMIT_MB+(2*(4096*4096-3072*3264)*EYE_COMMIT_BYTES >> 20))
        # Measured October 8: 2.5-2.6 GB more for eyes of 150% of 3072 x 3264.
        self.assertTrue(2500 < vr_commit_mb([3072, 3264], 150)-VR_COMMIT_MB < 2800)
        # The console says what the size is of, and when the runtime took less.
        self.assertEqual(rendering_line(4608, 4896, 150, 0, [3072, 3264]),
                         "Rendering 4608 x 4896 pixels per eye (150% of the headset's 3072 x 3264).")
        self.assertEqual(rendering_line(4000, 4248, 150, 0, [3072, 3264]),
                         "Rendering 4000 x 4248 pixels per eye (150% of the headset's 3072 x 3264 would be "
                         '4608 x 4896, more than the VR runtime takes).')
        self.assertEqual(rendering_line(2048, 2048, 100, 2048, [2496, 2688]),
                         "Rendering 2048 x 2048 pixels per eye (a square size instead of the headset's).")
        self.assertEqual(rendering_line(3072, 3264, 100, 0, None), 'Rendering 3072 x 3264 pixels per eye.')

    def test_the_render_scale_is_checked_before_anything_starts(self):
        import run_game_vr
        for argv in (['--render-scale', '49'], ['--render-scale', '201'], ['--size', '2048', '--render-scale', '125'],
                     ['--size', '8193']):
            with patch.object(sys, 'argv', ['run_game_vr.py', *argv]), patch('sys.stderr'), \
                    patch.object(run_game_vr, 'session') as session, self.assertRaises(SystemExit):
                run_game_vr.main()
            session.assert_not_called()

    def test_vr_starts_once_the_game_draws_frames_and_shows_one_queue(self):
        import run_game_vr
        game = Mock()
        # The renderer runs (30 frames) before the queue is looked for; startup can show no queue
        # or two for a moment.
        frames = [None, dict(frames=3), dict(frames=40), dict(frames=80), dict(frames=120)]
        with patch.object(run_game_vr, 'alive', return_value=True), \
             patch.object(run_game_vr, 'render_memory_snapshot', side_effect=frames) as memory, \
             patch.object(run_game_vr, 'discover_queue',
                          side_effect=[RuntimeError('Exactly one direct game queue required'), 0x1234]) as queue, \
             patch.object(run_game_vr.time, 'sleep'):
            self.assertEqual(run_game_vr.wait_for_renderer(game, 5, 0x3000), 0x1234)
        self.assertEqual((memory.call_count, queue.call_count), (4, 2))
        # Without the render memory module (an attached game), the queue alone decides.
        with patch.object(run_game_vr, 'alive', return_value=True), \
             patch.object(run_game_vr, 'discover_queue', return_value=0x99) as queue:
            self.assertEqual(run_game_vr.wait_for_renderer(game, 5, None), 0x99)
        with patch.object(run_game_vr, 'alive', side_effect=[True, False, False]), \
             patch.object(run_game_vr, 'discover_queue', side_effect=RuntimeError('no queue')), \
             patch.object(run_game_vr.time, 'sleep'):
            with self.assertRaisesRegex(RuntimeError, 'closed while starting'):
                run_game_vr.wait_for_renderer(game, 5, None)

    def test_renderer_wait_retries_passing_failures_but_not_a_call_the_game_never_answered(self):
        import run_game_vr
        game, problems = Mock(pid=7), []
        # A module list that fails once, then a queue; the report keeps each distinct problem.
        with patch.object(run_game_vr, 'alive', return_value=True), \
             patch.object(run_game_vr, 'discover_queue',
                          side_effect=[OSError('snapshot busy'), RuntimeError('2 direct queues'),
                                       RuntimeError('2 direct queues'), 0x55]), \
             patch.object(run_game_vr.time, 'sleep'), patch('builtins.print') as shown:
            self.assertEqual(run_game_vr.wait_for_renderer(game, 5, None, problems=problems, tell_every=0), 0x55)
        self.assertEqual(problems, ['snapshot busy', '2 direct queues'])
        shown.assert_called_with("Still looking for the game's graphics queue: 2 direct queues", flush=True)
        # A remote call that timed out leaves a thread waiting in the game: no second one.
        with patch.object(run_game_vr, 'alive', return_value=True), \
             patch.object(run_game_vr, 'discover_queue', side_effect=TimeoutError('Remote call timed out')) as queue:
            with self.assertRaisesRegex(RuntimeError, "did not answer Spidy's render probe: Remote call timed out"):
                run_game_vr.wait_for_renderer(game, 5, None)
        self.assertEqual(queue.call_count, 1)
        # A game that draws nothing is brought to the front, once.
        with patch.object(run_game_vr, 'alive', return_value=True), \
             patch.object(run_game_vr, 'discover_queue',
                          side_effect=[RuntimeError('the game submitted no graphics work in a second')]*3+[0x66]), \
             patch.object(run_game_vr, 'bring_to_front', return_value=False) as front, \
             patch.object(run_game_vr.time, 'sleep'):
            self.assertEqual(run_game_vr.wait_for_renderer(game, 5, None), 0x66)
        front.assert_called_once_with(7)

    def test_the_game_queue_is_the_one_the_game_submits_on_itself(self):
        from probe_stereo_gpu import game_queue
        sample = lambda queue, count, *stack: dict(kind=2, object=hex(queue), count=count, stack=list(stack))
        game = ('Spider-Man.exe+0x1c00', 'Spider-Man.exe+0x2000')
        # Alone, as on every tested PC.
        self.assertEqual(game_queue([sample(0xA, 900, *game), sample(0xA, 40, 'sl.interposer.dll+0x10', *game)]), 0xA)
        # An overlay or frame generation submitting on its own queue from inside the game's Present.
        overlay = sample(0xB, 120, 'gameoverlayrenderer64.dll+0x500', 'dxgi.dll+0x40', 'sl.interposer.dll+0x9',
                         'Spider-Man.exe+0x3000')
        self.assertEqual(game_queue([overlay, sample(0xA, 900, *game)]), 0xA)
        # Two queues the game submits on itself: the one with four times the work, else none.
        self.assertEqual(game_queue([sample(0xA, 900, *game), sample(0xC, 200, *game)]), 0xA)
        with self.assertRaisesRegex(RuntimeError, r'2 direct queues and none clearly the game.s: 0xa with 900 '
                                                  r'submissions, 0 calls below the game, called from Spider-Man\.exe'):
            game_queue([sample(0xA, 900, *game), sample(0xC, 400, *game)])
        with self.assertRaisesRegex(RuntimeError, 'no graphics work'):
            game_queue([dict(kind=1, object='0x9', count=5, stack=list(game))])

    def test_a_game_that_never_reads_spidys_controller_is_reported_once(self):
        from run_game_vr import pad_ignored
        watch = dict(reads=None, since=0.0, warned=False)
        sample = lambda seconds, reads, buttons='0x0', shown='game_screen': dict(
            seconds=seconds, pad_installed=True, pad_reads=reads, pad_buttons=buttons, presentation=shown)
        # Reads that grow (the game in front, on XInput) never warn.
        self.assertFalse(any(pad_ignored(watch, sample(t, t*2500, '0x1000')) for t in range(20)))
        # The Steam Link session: buttons held, reads frozen.
        watch = dict(reads=None, since=0.0, warned=False)
        results = [pad_ignored(watch, sample(t, 0, '0x1000' if t >= 3 else '0x0')) for t in range(12)]
        self.assertEqual(results.count(True), 1)
        self.assertTrue(results[5])
        # Immersive play reads the controller its own way; no buttons, no warning.
        watch = dict(reads=None, since=0.0, warned=False)
        self.assertFalse(any(pad_ignored(watch, sample(t, 0, '0x0')) for t in range(12)))
        self.assertFalse(any(pad_ignored(watch, sample(t, 0, '0x1000', 'immersive')) for t in range(12)))

    def test_a_session_that_ends_before_vr_writes_why_with_the_game_log(self):
        import json
        from run_game_vr import Startup
        with tempfile.TemporaryDirectory() as folder:
            report = pathlib.Path(folder)/'game-vr-1.json'
            startup = Startup(report)
            startup.ended(KeyboardInterrupt())  # cancelled before the game started: nothing to say
            self.assertFalse(report.exists())
            startup.stage = "looking for the game's graphics queue"
            startup.fields.update(game_pid=42, xr_runtime=dict(name='SteamVR'), queue_search=['2 direct queues'])
            log = pathlib.Path(folder)/'game.log'
            log.write_text('[Startup] Distribution platform: Steam\n')
            with patch('run_game_vr.documents_folder', return_value=pathlib.Path(folder)/'none'), \
                 patch('run_game_vr.keep_game_log', side_effect=lambda r: keep_game_log(r, log)):
                startup.ended(RuntimeError('No game graphics queue within 180 seconds: 2 direct queues.'),
                              lambda pid: [dict(name='Spider-Man.exe'), dict(name='RTSSHooks64.dll')])
            written = json.loads(report.read_text())
            self.assertEqual((written['stage'], written['vr_started'], written['xr_runtime']['name']),
                             ("looking for the game's graphics queue", False, 'SteamVR'))
            self.assertEqual(written['game_modules'], ['RTSSHooks64.dll', 'Spider-Man.exe'])
            self.assertIn('Distribution platform', report.with_name('game-vr-1-game.log').read_text())
            startup.ended(RuntimeError('later'))  # written once
            self.assertEqual(json.loads(report.read_text())['error'], written['error'])

    def test_a_game_that_already_had_vr_is_refused_before_the_headset_check(self):
        import argparse
        import run_game_vr
        from run_game_vr import had_vr, Startup
        # STOP VR, or VR that ended, leaves the game running with Spidy's XR module in it.
        self.assertTrue(had_vr(7, lambda pid: [dict(name='Spider-Man.exe'), dict(name='SPIDY_STEREO_PROBE.dll')]))
        self.assertFalse(had_vr(7, lambda pid: [dict(name='Spider-Man.exe'), dict(name='spidy_bridge.dll')]))
        # A game closing or still being set up lists no modules: the XR start decides (1000 says the same).
        self.assertFalse(had_vr(7, Mock(side_effect=OSError('snapshot failed'))))
        with tempfile.TemporaryDirectory() as folder:
            startup = Startup(pathlib.Path(folder)/'game-vr-3.json')
            with patch.object(run_game_vr, 'game_running', return_value=True), \
                 patch.object(run_game_vr, 'find_game', return_value=7), \
                 patch.object(run_game_vr, 'had_vr', return_value=True) as had, \
                 patch.object(run_game_vr, 'preflight') as preflight:
                with self.assertRaisesRegex(RuntimeError, 'Close Spider-Man, then press START VR'):
                    run_game_vr.session(argparse.Namespace(stop_event=None), startup)
            had.assert_called_once_with(7)
            preflight.assert_not_called()
            self.assertEqual(startup.stage, 'checking the running game')

    def test_the_console_goes_to_a_log_beside_the_report(self):
        import io
        import run_game_vr
        with tempfile.TemporaryDirectory() as folder:
            report = pathlib.Path(folder)/'reports'/'game-vr-2.json'
            stdout, stderr = sys.stdout, sys.stderr
            shown, errors = sys.stdout, sys.stderr = io.StringIO(), io.StringIO()
            try:
                path = run_game_vr.keep_console(report)
                print('VR runtime: SteamVR')
                print('Spidy VR unavailable: x', file=sys.stderr)
                sys.stdout.log.close()
            finally:
                sys.stdout, sys.stderr = stdout, stderr
            self.assertEqual((shown.getvalue(), errors.getvalue()), ('VR runtime: SteamVR\n', 'Spidy VR unavailable: x\n'))
            self.assertEqual(path.read_text(encoding='utf-8'), 'VR runtime: SteamVR\nSpidy VR unavailable: x\n')

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
        for reader, magic, size in ((gpu_snapshot, 0x53475044, 208), (xr_snapshot, 0x53585244, 800),
                                     (collision_snapshot, 0x53435044, 2160),
                                     (movement_snapshot, 0x534d5044, 57408),
                                     (motion_snapshot, 0x534d5644, 176), (ray_snapshot, 0x53525944, 784),
                                     (swing_snapshot,0x53574441,240),(timing_snapshot,0x5358544d,344),
                                     (appearance_snapshot,0x53415044,328),(eye_snapshot,0x53455353,112),
                                     (grab_snapshot,0x53475244,368),(body_snapshot,0x53424453,144),
                                     (punch_snapshot,0x53505544,160),(shooter_snapshot,0x53484f44,192)):
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

    def test_flight_summary_counts_steps_that_ran_without_the_command(self):
        def step(index, driven, velocity, status=2, contact=2):
            return dict(steps=index, controlled=driven, status=status, contact=contact, velocity=velocity)
        # Steady flight, sampled every step: gravity only, no zigzag, nothing dropped.
        steady = [step(100+i, 50+i, (10, -.1*i, 0)) for i in range(6)]
        summary = flight_summary(steady)
        self.assertEqual((summary['controlled_air_steps'], summary['steps_without_command']), (6, 0))
        self.assertAlmostEqual(summary['alternation_mps'], 0)
        # Step 103 ran on the game's own fall speed: its sample is not a controlled one, and
        # the count of controlled steps fell one behind the count of steps.
        hitch = steady[:3]+[step(103, 52, (4, -28, 0), status=1)]+[step(104+i, 53+i, (10, -.4, 0)) for i in range(3)]
        self.assertEqual(flight_summary(hitch)['steps_without_command'], 1)
        # A sparse sampler sees only the counters.
        self.assertEqual(flight_summary([steady[0], step(104, 53, (10, -.4, 0))])['steps_without_command'], 1)
        # Separate flights are not one flight with thousands of missed steps.
        self.assertEqual(flight_summary([steady[0], step(900, 60, (10, 0, 0))])['steps_without_command'], 0)

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
        raw = bytearray(800)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 19, 800, 3, 4)
        self.assertIsNone(xr_snapshot(Reader(*([raw, struct.pack('<Q', 6)]*8)), 0))

    def test_xr_decodes_both_hands_and_status_message(self):
        raw = bytearray(800)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 19, 800, 3, 4)
        struct.pack_into('<16f', raw, 160, *range(16))
        struct.pack_into('<16f', raw, 224, *range(16, 32))
        raw[288:295] = b'Tracked'
        struct.pack_into('<2Q',raw,544,50,90)
        struct.pack_into('<4I',raw,560,1,7,2688,2784)
        struct.pack_into('<2IQ', raw, 576, 3, 0, 1234)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['hands'][0][12], result['hands'][1][12]), (12, 28))
        self.assertEqual(result['message'], 'Tracked')
        self.assertEqual((result['unique_submitted'],result['reused_submitted']),(50,90))
        self.assertEqual((result['flat_screen'],result['toggles'],result['eye_width'],result['eye_height']),(1,7,2688,2784))
        # The game's camera on the virtual screen while gameplay was unavailable.
        self.assertEqual((result['presentation'], result['screen_submitted']), ('game_screen', 1234))
        self.assertEqual((result['gate'], result['camera_mover']), ([], None))

    def test_xr_says_why_gameplay_was_unavailable_and_which_camera_ran(self):
        raw = bytearray(800)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 19, 800, 3, 4)
        # A played scene whose camera the gate does not accept, the third player of the session,
        # and the menu button held on the virtual controller the game has read 900 times.
        struct.pack_into('<2I2Q2IQ', raw, 592, 8, 0x3872860, 3, 0x2aefe723280, 0x10, 1, 900)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['gate'], result['camera_mover']), (['other_camera'], 'exterior'))
        self.assertEqual((result['players'], result['player_record']), (3, '0x2aefe723280'))
        self.assertEqual((result['pad_buttons'], result['pad_installed'], result['pad_reads']), ('0x10', True, 900))
        # The player's follow camera commits too, before the other camera each frame.
        struct.pack_into('<2Q', raw, 632, 4800, 2400)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['camera_commits'], result['player_commits']), (4800, 2400))
        # No player and no camera commit; an unnamed camera keeps its offset.
        struct.pack_into('<2I', raw, 592, 5, 0x3999999)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['gate'], result['camera_mover']), (['no_player', 'no_camera_commit'], '0x3999999'))

    def test_xr_counts_interacts_and_aim_markers(self):
        raw = bytearray(800)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 19, 800, 3, 4)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['interacts'], result['aim_markers'], result['markers']), (0, False, 0))
        # Three B presses reached the game as its Y; markers on, 5000 drawn so far.
        struct.pack_into('<2IQ', raw, 648, 3, 1, 5000)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['interacts'], result['aim_markers'], result['markers']), (3, True, 5000))

    def test_xr_reports_the_vr_settings_and_the_settings_tab(self):
        raw = bytearray(800)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 19, 800, 3, 4)
        # Markers on; web grab, webs in open air, the web shooter and body on, punching off; 48 m/s, half again
        # real gravity, 45 degree snap turns, smooth turning at 120 degrees a second, half vibration, the large
        # screen; two changes in the SPIDY VR tab, which the game built 7 times.
        struct.pack_into('<2IQ', raw, 648, 0, 1, 0)
        struct.pack_into('<4IfIQ2I', raw, 664, 29, 45, 50, 2, 48.0, 2, 7, 1, 0)
        struct.pack_into('<I', raw, 704, 120)
        # Half again real gravity, the large HUD; flips at 300 degrees a second.
        struct.pack_into('<2I', raw, 744, 150, 3)
        struct.pack_into('<I', raw, 792, 300)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual(result['vr_settings'], dict(aim_markers=True, web_grab=True, air_webs=True, web_shooter=True,
                                                     punch=False, body=True, swing_speed=48.0, weight=150,
                                                     snap_turn=45, smooth_turn=120, haptics=50, screen_size=2,
                                                     flips=False, flip_speed=300, trigger_webs=False, hud=3,
                                                     eye_height_mm=0, arm_length_mm=0, calibration_prompt=True))
        # The HUD switched off in the tab.
        struct.pack_into('<I', raw, 748, 0)
        self.assertEqual(xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)['vr_settings']['hud'], 0)
        self.assertEqual((result['setting_changes'], result['menu_tabs'], result['menu_installed'],
                          result['menu_status']), (2, 7, True, 0))
        # The tab could not be hooked: the game's code at its second hook is not the supported build's.
        struct.pack_into('<2I', raw, 696, 0, 9302)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['menu_installed'], result['menu_status']), (False, 9302))
        # Webs in open air and the web shooter switched off: a web that meets nothing misses, and a free hand's
        # trigger shoots nothing.
        struct.pack_into('<I', raw, 664, 5)
        settings = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)['vr_settings']
        self.assertFalse(settings['air_webs'] or settings['web_shooter'] or settings['flips'] or
                         settings['trigger_webs'])
        # The experimental flips switched on.
        struct.pack_into('<I', raw, 664, 5 | 32)
        self.assertTrue(xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)['vr_settings']['flips'])
        # WEB BUTTON: TRIGGER, the trigger webbing and the grip reeling; nothing else changed.
        struct.pack_into('<I', raw, 664, 5 | 64)
        settings = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)['vr_settings']
        self.assertTrue(settings['trigger_webs'])
        self.assertFalse(settings['flips'] or settings['air_webs'])

    def test_xr_reports_walls_the_game_held_the_player_on(self):
        raw = bytearray(800)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 19, 800, 3, 4)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual(result['surface'], dict(entries=0, frames=0, hero_up=[0, 0, 0], stand_off_m=0,
                                                 head_height_m=0, head_clearance_m=0))
        # Two stretches on a wall, 450 frames in all; the player's up along the wall's normal (+x); the head
        # 0.48 m off it, placed from the feet it would have been 0.02 m inside it.
        struct.pack_into('<IQ3f3f', raw, 708, 2, 450, 1, 0, 0, .48, -.02, .46)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual(result['surface'], dict(entries=2, frames=450, hero_up=[1, 0, 0], stand_off_m=.48,
                                                 head_height_m=-.02, head_clearance_m=.46))

    def test_xr_reports_the_t_pose_calibration(self):
        raw = bytearray(800)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 19, 800, 3, 4)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual(result['calibration'], dict(phase='none', hint=None, progress=0, done=0, skipped=0,
                                                     reach_m=[0, 0]))
        self.assertEqual({k: result['vr_settings'][k] for k in ('eye_height_mm', 'arm_length_mm', 'calibration_prompt')},
                         dict(eye_height_mm=0, arm_length_mm=0, calibration_prompt=True))
        # The panel shows, asking for straight arms, the hold a third full.
        struct.pack_into('<2If', raw, 752, 1, 4, .3333)
        calibration = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)['calibration']
        self.assertEqual((calibration['phase'], calibration['hint'], calibration['progress']), ('waiting', 'straight', .333))
        # Measured: eyes 1.63 m high, arms 0.59 m (the left 0.585, the right 0.59); the result still showing.
        struct.pack_into('<2If5I2f', raw, 752, 3, 0, 1, 1630, 590, 1, 0, 0, .585, .59)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual(result['calibration'], dict(phase='done', hint=None, progress=1, done=1, skipped=0,
                                                     reach_m=[.585, .59]))
        self.assertEqual((result['vr_settings']['eye_height_mm'], result['vr_settings']['arm_length_mm']), (1630, 590))
        # Skipped: no calibration, and none asked for at the next sessions' first gameplay.
        struct.pack_into('<2If5I2f', raw, 752, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['calibration']['skipped'], result['vr_settings']['calibration_prompt']), (1, False))

    def test_settings_the_headset_changed_go_to_the_launcher_on_one_line(self):
        args = Mock(no_aim_markers=False, no_web_grab=False, no_air_webs=False, no_web_shooter=False, no_punch=True,
                    no_body=False, swing_speed=32.0, weight=60, snap_turn=30, smooth_turn=0, haptics=100,
                    screen_size=1, flips=False, flip_speed=180, trigger_webs=False, hud=2, eye_height=0,
                    arm_length=0, no_calibration_prompt=False)
        start = start_settings(args)
        self.assertEqual(start, dict(aim_markers=True, web_grab=True, air_webs=True, web_shooter=True, punch=False,
                                     body=True, swing_speed=32.0, weight=60, snap_turn=30, smooth_turn=0,
                                     haptics=100, screen_size=1, flips=False, flip_speed=180, trigger_webs=False,
                                     hud=2, eye_height_mm=0, arm_length_mm=0, calibration_prompt=True))
        # Nothing changed, or no sample: no line.
        self.assertIsNone(settings_line(start, dict(vr_settings=dict(start))))
        self.assertIsNone(settings_line(start, None))
        changed = dict(start, air_webs=False, web_shooter=False, punch=True, swing_speed=48.0, weight=150,
                       snap_turn=0, smooth_turn=90)
        self.assertEqual(settings_line(start, dict(vr_settings=changed)),
                         SETTINGS_LINE + 'aim_markers=1 web_grab=1 air_webs=0 web_shooter=0 punch=1 body=1 '
                                         'swing_speed=48 weight=150 snap_turn=0 smooth_turn=90 haptics=100 '
                                         'screen_size=1 flips=0 flip_speed=180 trigger_webs=0 hud=2 eye_height_mm=0 '
                                         'arm_length_mm=0 calibration_prompt=1')
        # The experimental flips switched on in the headset go to the launcher too; so do their speed, the web
        # button and the HUD.
        self.assertIn(' flips=1 ', settings_line(start, dict(vr_settings=dict(start, flips=True))))
        self.assertIn(' flip_speed=240 ', settings_line(start, dict(vr_settings=dict(start, flip_speed=240))))
        self.assertIn(' trigger_webs=1 ', settings_line(start, dict(vr_settings=dict(start, trigger_webs=True))))
        self.assertIn(' hud=0 ', settings_line(start, dict(vr_settings=dict(start, hud=0))))
        # A T-pose calibration in the headset goes to the launcher the same way; so does skipping it.
        self.assertTrue(settings_line(start, dict(vr_settings=dict(start, eye_height_mm=1630, arm_length_mm=590)))
                        .endswith(' eye_height_mm=1630 arm_length_mm=590 calibration_prompt=1'))
        self.assertTrue(settings_line(start, dict(vr_settings=dict(start, calibration_prompt=False)))
                        .endswith(' calibration_prompt=0'))


if __name__ == '__main__':
    unittest.main()

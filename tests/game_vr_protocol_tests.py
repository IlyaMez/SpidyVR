"""Reject torn or incompatible telemetry before it drives integration decisions."""
import math
import pathlib
import re
import struct
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, Mock, patch
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from probe_stereo_gpu import snapshot as gpu_snapshot, save_eye_images
from run_game_vr import (snapshot as xr_snapshot, accepted as accepted_xr, timing_snapshot, frame_rates,
                         appearance_snapshot, eye_snapshot, save_eye_snapshot, rgb_rows, crop_origin,
                         game_memory, keep_game_log, body_snapshot,
                         punch_snapshot, strike_snapshot, shooter_snapshot, rope_snapshot, start_settings,
                         settings_line,
                         SETTINGS_LINE,
                         scaled_eye_size, rendering_line, eye_render_size, eye_reflections, XR_STAGES,
                         command_text, network_links, link_warning, stream_stalls,
                         slow_motion_snapshot, hud_snapshot, GAME_HOOKS, controllers_line,
                         ray_traced_view, RAY_TRACING_OFF, ASPECT_RATIO_AUTO, other_mods, real_vr_refusal,
                         other_mods_warning, REFLEX_OFF, device_removed, device_lost_advice, game_adapters,
                         demand_name, reachable_in_play, prompts_line, prompt_snapshot,
                         controller_buttons, sense_snapshot, attack_flags)
from probe_collision import snapshot as collision_snapshot
from probe_movement import snapshot as movement_snapshot
from probe_native_motion import snapshot as motion_snapshot
from capture_game_state import Game, LIVE_VTABLES
from probe_native_rays import snapshot as ray_snapshot, command as ray_command
from probe_game_swing import (snapshot as swing_snapshot, command as swing_command, assess as assess_swing,
                              flight_summary, climb_snapshot)
from probe_game_grab import grab_snapshot, hand_command as grab_hand_command


class Reader:
    def __init__(self, *values):
        self.values = iter(values)

    def read(self, address, size):
        return next(self.values)


class ProtocolTests(unittest.TestCase):
    def test_wall_roll_flag_uses_version_5_and_does_not_change_legacy_commands(self):
        old = swing_command(1, (0, 0, 0))
        self.assertEqual(len(old), 168)
        packet = swing_command(1, (0, 0, 0), wall_rotate=True)
        self.assertEqual(len(packet), 240)
        self.assertEqual(struct.unpack_from('<3I', packet), (0x5357434d, 5, 240))
        self.assertEqual(struct.unpack_from('<I', packet, 232), (1,))
        self.assertEqual(struct.unpack_from('<4f', packet, 168), (0, 0, 0, 1))
        with self.assertRaises(ValueError):
            swing_command(1, (0, 0, 0), wall_rotate=2)

    def test_wall_contact_snapshot_reads_both_palms_and_counters(self):
        raw = struct.pack('<4IQ2Q2I', 0x53434c44, 1, 48, 1, 2, 7, 9, 3, 0)
        result = climb_snapshot(Reader(raw, struct.pack('<Q', 2)), 0)
        self.assertEqual(result, dict(status=1, contacts=[7, 9], hands=3))

    def test_wall_contact_snapshot_rejects_incompatible_and_torn_data(self):
        raw = struct.pack('<4IQ2Q2I', 0x53434c44, 2, 48, 1, 2, 7, 9, 3, 0)
        with self.assertRaisesRegex(RuntimeError, 'protocol mismatch'):
            climb_snapshot(Reader(raw), 0)
        odd = struct.pack('<4IQ2Q2I', 0x53434c44, 1, 48, 1, 3, 7, 9, 3, 0)
        self.assertIsNone(climb_snapshot(Reader(*([odd] * 8)), 0))
        bad = struct.pack('<4IQ2Q2I', 0x53434c44, 1, 48, 1, 2, 7, 9, 4, 0)
        with self.assertRaisesRegex(RuntimeError, 'Invalid wall contact'):
            climb_snapshot(Reader(bad, struct.pack('<Q', 2)), 0)

    def test_appearance_decodes_each_eye_and_rejects_torn_reads(self):
        raw = bytearray(360)
        struct.pack_into('<4I6Q', raw, 0, 0x53415044, 6, 360, 1, 6, 11, 12, 21, 22, 0x123456789)
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
        self.assertEqual((result['eye_detail_frames'], result['texture_request_jobs'], result['impostor_frames']),
                         (0, 0, 0))
        struct.pack_into('<2f2QfI', raw, 328, .5863, 1.7056, 900, 1800, 1.5, 850)
        result = appearance_snapshot(Reader(raw, struct.pack('<Q', 6)), 0)
        self.assertEqual((result['eye_distance_scale'], result['eye_detail'], result['eye_detail_frames']),
                         (.5863, 1.7056, 900))
        self.assertEqual((result['texture_request_jobs'], result['texture_request_raise']), (1800, 1.5))
        self.assertEqual(result['impostor_frames'], 850)
        struct.pack_into('<2I4Q', raw, 176, 2, 2, 5, 4, 1, 900)
        struct.pack_into('<2Q5f', raw, 216, 120, 3, -1, 1, -.5, 2, 3.5)
        result = appearance_snapshot(Reader(raw, struct.pack('<Q', 6)), 0)
        self.assertEqual((result['web_state'], result['web_live']), ('game_webs', [False, True]))
        self.assertEqual((result['web_creates'], result['web_releases'], result['web_failures'], result['web_updates']),
                         (5, 4, 1, 900))
        self.assertEqual((result['active_view_aligned'], result['active_view_rejected']), (120, 3))
        self.assertEqual(result['active_view_shift_m'], 3.5)
        self.assertEqual(result['exposure_shared'], 0)
        struct.pack_into('<I', raw, 252, 240)
        self.assertEqual(appearance_snapshot(Reader(raw, struct.pack('<Q', 6)), 0)['exposure_shared'], 240)
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
        raw = bytearray(272)
        struct.pack_into('<4IQ9Q10f2I', raw, 0, 0x53574441,4,272,2,4,
                         *range(1,10), *range(10,19), .01,1,0)
        struct.pack_into('<2I5f',raw,144,1,0x3000123,1,2,3,40,50)
        struct.pack_into('<2I5f',raw,172,0,0xffffffff,4,5,6,70,80)
        struct.pack_into('<4I',raw,224,2,1,7,0)
        struct.pack_into('<I4f3I',raw,240,1,-1,0,0,.9,3,2,1)
        result = swing_snapshot(Reader(raw,struct.pack('<Q',4)),0)
        self.assertEqual((result['wall'],result['wall_normal'],result['walls'],result['wall_jumps'],
                          result['mount']),(1,(-1,0,0),3,2,1))
        self.assertAlmostEqual(result['wall_distance'],.9,places=6)
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
        raw = bytearray(168)
        struct.pack_into('<4Iq4Q2I', raw, 0, 0x53505544, 1, 168, 2, 4, 5000, 3, 3, 1, 2, 0)
        # Hands (32 bytes): punches, last target, last strength, speed, last knockback, busy.
        struct.pack_into('<2Q2f2I', raw, 64, 1, 0x1c517822940, .25, .5, 2, 0)
        struct.pack_into('<2Q2f2I', raw, 96, 2, 0x1c517822a00, 1., 9.5, 5, 1)
        struct.pack_into('<3f3f2fQ', raw, 128, 1.5, 95.25, 2393.75, 0, 0, -1, 40, 8.25, 2)
        result = punch_snapshot(Reader(raw, struct.pack('<q', 4)), 0)
        self.assertEqual((result['punches'], result['issued'], result['dropped'], result['bots']), (3, 3, 1, 2))
        # Two of the three issued went out without their damager.
        self.assertEqual(result['unnamed'], 2)
        self.assertEqual((result['hands'][0]['punches'], result['hands'][0]['last_knockback']), (1, 2))
        self.assertEqual(result['hands'][1]['last_target'], '0x1c517822a00')
        self.assertEqual((result['hands'][1]['speed'], result['hands'][1]['busy']), (9.5, True))
        self.assertEqual((result['last_point'], result['last_direction']), ([1.5, 95.25, 2393.75], [0, 0, -1]))
        self.assertEqual((result['last_damage'], result['last_speed']), (40, 8.25))
        torn = bytearray(raw)
        struct.pack_into('<q', torn, 16, 5)
        self.assertIsNone(punch_snapshot(Reader(*([torn, struct.pack('<q', 5)]*8)), 0))

    def test_strike_feedback_decodes_the_strikes_the_blows_and_the_latest_of_each(self):
        raw = bytearray(328)
        struct.pack_into('<4Iq', raw, 0, 0x53535452, 3, 328, 2, 8)
        # Opened, landed, missed, held; blows and their kinds; thugs' flights given and refused.
        struct.pack_into('<4Q', raw, 24, 7, 5, 2, 1)
        struct.pack_into('<8Q', raw, 56, 19, 3, 2, 4, 1, 3, 9, 1)
        # Phase, hand, flow, enemies; the strike's target and where the player stops.
        struct.pack_into('<4IQ3f2f', raw, 120, 1, 1, 4, 6, 0x1c517822940, 10.5, 2.25, -40, 6.5, .25)
        struct.pack_into('<3f3f2f5I', raw, 164, 11, 3.5, -41, 0, 0, -1, .75, 62.5, 5, 5, 1, 1, 0)
        result = strike_snapshot(Reader(raw, struct.pack('<q', 8)), 0)
        self.assertEqual((result['opened'], result['landed'], result['missed'], result['held']), (7, 5, 2, 1))
        self.assertEqual((result['blows'], result['heavies'], result['launches'], result['juggles'],
                          result['slams'], result['finishers']), (19, 3, 2, 4, 1, 3))
        self.assertEqual((result['flown'], result['unflown']), (9, 1))
        self.assertEqual((result['phase'], result['hand'], result['flow'], result['enemies']), ('flying', 1, 4, 6))
        self.assertEqual((result['target'], result['destination']), ('0x1c517822940', [10.5, 2.25, -40]))
        self.assertEqual((result['distance'], result['flight_seconds']), (6.5, .25))
        self.assertEqual((result['last_point'], result['last_direction']), ([11, 3.5, -41], [0, 0, -1]))
        self.assertEqual((result['last_strength'], result['last_damage']), (.75, 62.5))
        self.assertEqual((result['last_kind'], result['last_count'], result['last_hand'],
                          result['last_by_strike']), ('finisher', 5, 1, True))
        # Called blows: none made yet, and no call showing.
        self.assertEqual((result['calls'], result['called'], result['missed_calls'], result['last_called'],
                          result['cue']), (0, 0, 0, False, None))
        # Nine calls, six answered, two taken away by another blow; the latest blow was a called one, and the
        # finisher is called on a thug now: a fist to the right, with 0.75 of its time left.
        struct.pack_into('<I', raw, 212, 1)
        struct.pack_into('<4Q3f2If', raw, 216, 9, 6, 2, 0x1c517822940, 11, 3.75, -41, 2, 1, .75)
        result = strike_snapshot(Reader(raw, struct.pack('<q', 8)), 0)
        self.assertEqual((result['calls'], result['called'], result['missed_calls'], result['last_called']),
                         (9, 6, 2, True))
        self.assertEqual(result['cue'], dict(target='0x1c517822940', way='right', finisher=True, left=.75,
                                             locked=False, point=[11, 3.75, -41]))
        # One enemy at a time: nobody locked, nobody knocked out yet.
        self.assertEqual((result['locked'], result['knockouts'], result['swung'], result['strike_called'],
                          result['passed_over']), ('0x0', 0, 0, 0, 0))
        # A blow locked the player onto that thug; four knocked out, the latest at a place; three strikes opened
        # by a fist swung the called way, two of whose blows answered; five swings at others opened nothing.
        struct.pack_into('<2Q3fI3Q', raw, 272, 0x1c517822940, 4, 12, 3.25, -42, 1, 3, 2, 5)
        result = strike_snapshot(Reader(raw, struct.pack('<q', 8)), 0)
        self.assertEqual((result['locked'], result['knockouts'], result['knockout_point']),
                         ('0x1c517822940', 4, [12, 3.25, -42]))
        self.assertEqual((result['cue']['locked'], result['swung'], result['strike_called'], result['passed_over']),
                         (True, 3, 2, 5))
        torn = bytearray(raw)
        struct.pack_into('<q', torn, 16, 9)
        self.assertIsNone(strike_snapshot(Reader(*([torn, struct.pack('<q', 9)]*8)), 0))

    def test_spider_sense_decodes_the_warnings_the_counters_and_the_census(self):
        raw = bytearray(632)
        struct.pack_into('<4Iq', raw, 0, 0x53534e53, 1, 632, 2, 6)
        # 900 samples, 880 read the hero's manager, 3 faults; one manager, at most 3 attacks, 2 now, 2 standing.
        struct.pack_into('<3Q4IQd', raw, 24, 900, 880, 3, 1, 3, 2, 2, 0x1c5a0001230, 812.5)
        # 7 warnings: 4 asked for a counter, 2 a dodge, 1 a jump; 1 named no actor in play. Counters: 2 thrown,
        # 1 landed, 1 perfect; 3 counter blows, 2 breaks; the latest at a thug, his middle, 0.18 s left, right hand.
        struct.pack_into('<5Q', raw, 80, 7, 0, 4, 2, 1)
        struct.pack_into('<7Q3ffI', raw, 120, 1, 2, 1, 1, 3, 2, 0x1c517822940, 4, 1, -6, .18, 1)
        # The soonest: a thug's counterable punch 0.25 s off; then a jump attack with no time set.
        struct.pack_into('<Q3f3f2f2I4Bf', raw, 200, 0x1c517822940, 4, 1.75, -6, 4, 1, -6, .25, .4, 0x3, 0x10,
                         1, 1, 0, 0, .375)
        struct.pack_into('<Q3f3f2f2I4Bf', raw, 256, 0x1c517822a00, -3, 1.8, 2, -3, 1, 2, 0, .2, 0x11, 0x10,
                         3, 0, 1, 0, 1)
        # Census: the counterable punch 5 times, up to 0.65 s of warning; the jump attack twice, untimed.
        struct.pack_into('<8I', raw, 536, 0x3, 0x11, 0, 0, 0, 0, 0, 0)
        struct.pack_into('<8I', raw, 568, 5, 2, 0, 0, 0, 0, 0, 0)
        struct.pack_into('<8f', raw, 600, .65, -1, 0, 0, 0, 0, 0, 0)
        result = sense_snapshot(Reader(raw, struct.pack('<q', 6)), 0)
        self.assertEqual((result['samples'], result['read'], result['faults'], result['managers'],
                          result['most_attacks'], result['attacks']), (900, 880, 3, 1, 3, 2))
        self.assertEqual((result['manager'], result['clock']), ('0x1c5a0001230', 812.5))
        self.assertEqual(result['asked'], dict(none=0, counter=4, dodge=2, jump=1))
        self.assertEqual((result['warnings'], result['unknown']), (7, 1))
        self.assertEqual((result['thrown'], result['landed'], result['perfect'], result['counter_blows'],
                          result['breaks']), (2, 1, 1, 3, 2))
        self.assertEqual((result['last_counter'], result['last_point'], result['last_left'], result['last_hand']),
                         ('0x1c517822940', [4, 1, -6], .18, 'right'))
        first, second = result['standing']
        self.assertEqual((first['answer'], first['to_impact'], first['flags'], first['state'], first['left']),
                         ('counter', .25, ['dodgeable', 'counterable'], '0x10', .375))
        self.assertEqual((second['answer'], second['to_impact'], second['countered']), ('jump', None, True))
        self.assertEqual(result['census'], [dict(flags=['dodgeable', 'counterable'], bits='0x3', count=5,
                                                 longest=.65),
                                            dict(flags=['dodgeable', 'jump_button'], bits='0x11', count=2,
                                                 longest=None)])
        # Bits above the game's own thirteen are kept as one number.
        self.assertEqual(attack_flags(0x103c487), ['dodgeable', 'counterable', 'never_beaten', 'dodge_away',
                                                   'reversable', '0x103c000'])
        torn = bytearray(raw)
        struct.pack_into('<q', torn, 16, 7)
        self.assertIsNone(sense_snapshot(Reader(*([torn, struct.pack('<q', 7)]*8)), 0))
        # Another build's struct is reported, and never ends the session.
        other = bytearray(raw)
        struct.pack_into('<I', other, 4, 2)
        self.assertEqual(sense_snapshot(Reader(other), 0),
                         dict(protocol_mismatch=dict(magic='0x53534e53', version=2, bytes=632)))

    def test_ropes_decode_bends_pay_out_and_a_web_in_both_hands(self):
        raw = bytearray(96)
        # Physical; wraps, unwraps, cut, paid out; each hand's bends, free and whole rope; joins, shared; the
        # thin things arcade webs went through.
        struct.pack_into('<4Iq4Q2I4fQ2I', raw, 0, 0x53575250, 3, 96, 1, 8, 15, 3, 1, 66, 2, 0, 12.5, 30.25, 40, 30.25,
                         4, 1, 9)
        result = rope_snapshot(Reader(raw, struct.pack('<q', 8)), 0)
        self.assertEqual((result['physical'], result['wraps'], result['unwraps'], result['cut'], result['paid_out']),
                         (True, 15, 3, 1, 66))
        self.assertEqual((result['bends'], result['free_m'], result['whole_m']), ([2, 0], [12.5, 30.25], [40, 30.25]))
        self.assertEqual((result['joins'], result['shared'], result['passed']), (4, True, 9))
        # A build before arcade webs: its spare word is no count.
        before = bytearray(raw)
        struct.pack_into('<I', before, 4, 2)
        self.assertEqual(rope_snapshot(Reader(before, struct.pack('<q', 8)), 0)['passed'], 0)
        old = bytearray(raw)
        struct.pack_into('<2I', old, 4, 1, 80)
        with self.assertRaisesRegex(RuntimeError, 'Rope protocol mismatch'):
            rope_snapshot(Reader(old, struct.pack('<q', 8)), 0)
        torn = bytearray(raw)
        struct.pack_into('<q', torn, 16, 9)
        self.assertIsNone(rope_snapshot(Reader(*([torn, struct.pack('<q', 9)]*8)), 0))

    def test_slow_motion_decodes_presses_focus_and_the_games_time_and_rejects_torn_reads(self):
        raw = bytearray(120)
        struct.pack_into('<4Iq6I', raw, 0, 0x574f4c53, 2, 120, 0, 6, 3, 2, 1, 1, 0, 1)
        struct.pack_into('<7fI2Q', raw, 48, 4.5, .25, 1, .3, 1, .3, .01, 1, 9000, 1200)
        # Moments: four heavy blows, two called blows, one thug pulled in the air, one perfect dodge; one is on.
        struct.pack_into('<5If', raw, 96, 4, 2, 1, 1, 1, .5)
        result = slow_motion_snapshot(Reader(raw, struct.pack('<q', 6)), 0)
        self.assertEqual((result['status'], result['starts'], result['stops'], result['refused'],
                          result['emptied'], result['interrupted'], result['active']), (0, 3, 2, 1, 1, 0, True))
        self.assertEqual((result['seconds'], result['focus'], result['blend']), (4.5, .25, 1))
        self.assertAlmostEqual(result['wanted'], .3, 6)
        self.assertAlmostEqual(result['world_scale'], .3, 6)
        self.assertAlmostEqual(result['physics_step'], .01, 6)
        self.assertEqual((result['game_scale'], result['physics_scaled']), (1, True))
        self.assertEqual((result['updates'], result['slowed_updates']), (9000, 1200))
        self.assertEqual((result['moments'], result['moment_active'], result['moment_scale']),
                         (dict(blow=4, called_blow=2, air_pull=1, perfect_dodge=1), True, .5))
        torn = bytearray(raw)
        struct.pack_into('<q', torn, 16, 7)
        self.assertIsNone(slow_motion_snapshot(Reader(*([torn, struct.pack('<q', 7)]*8)), 0))
        wrong = bytearray(raw)
        struct.pack_into('<I', wrong, 4, 1)
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
        # last faced 1.5 degrees from the head and at most 31.25. The markers' head was at most 0.0625 m from
        # the frame's.
        struct.pack_into('<If3Q2f', raw, 152, 3, .0625, 0, 0, 2400, 1.5, 31.25)
        result = hud_snapshot(Reader(raw, struct.pack('<q', 6)), 0)
        self.assertEqual(result['marker_mismatch_m'], .0625)
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
        raw = bytearray(296)
        # 900 samples, 5 pulls, 4 shots fired, 1 dropped, 2 aimed at a thug, 1 target taken; the gadget, 4000
        # frames, 3 thugs on offer.
        struct.pack_into('<4Iq8Q2I', raw, 0, 0x53484f44, 4, 296, 2, 4, 900, 5, 4, 1, 2, 1, 0x1c5dd1c0560, 4000, 3, 0)
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
        # No gadgets read yet (v3).
        self.assertEqual((result['weapon_manager'], result['gadget_error'], result['equipped'], result['gadgets'],
                          result['equip_result'], result['equips'], result['gadget_shots'], result['empty_pulls']),
                         ('0x0', 0, 0, [], 0, 0, 0, 0))
        # The player's gadgets: the web shooter at the top with nothing to count, the impact web equipped at the
        # next place with 3 of 5 charges.
        struct.pack_into('<Q2I2HI', raw, 192, 0x1c5aa000000, 2, 0, 0x9c4a, 3, 4)
        struct.pack_into('<2Q', raw, 280, 6, 1)
        struct.pack_into('<H4B', raw, 216, 0x9c49, 1, 0, 0, 0)
        struct.pack_into('<H4B', raw, 224, 0x9c4a, 2, 1, 3, 5)
        result = shooter_snapshot(Reader(raw, struct.pack('<q', 4)), 0)
        self.assertEqual((result['weapon_manager'], result['equipped']), ('0x1c5aa000000', 0x9c4a))
        # The fourth equip asked for found no such gadget; six gadget shots, one pull without a charge.
        self.assertEqual((result['equip_result'], result['equips'], result['gadget_shots'], result['empty_pulls']),
                         (3, 4, 6, 1))
        self.assertEqual(result['gadgets'], [dict(gadget='web_shooter', weapon=0x9c49, place=0, ammo=0, most=0),
                                             dict(gadget='impact_web', weapon=0x9c4a, place=1, ammo=3, most=5)])
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

    def test_memory_windows_promised_a_process_is_read_for_the_reports(self):
        # This process stands in for the game.
        import os
        import run_game_vr
        self.assertTrue(4 < run_game_vr.process_commit_mb(os.getpid()) < 4096)
        self.assertIsNone(run_game_vr.process_commit_mb(0))  # no such process to ask
        self.assertGreater(run_game_vr.free_commit_mb(), 0)

    def test_a_pc_on_wifi_is_warned_about_and_stream_stalls_are_counted(self):
        wlan = '    State                  : connected\n    SSID                   : Home 5G\n'
        cable = 'Ethernet adapter Ethernet:\n\n   IPv4 Address. . . : 192.168.1.20\n'
        no_cable = 'Ethernet adapter Ethernet:\n\n   Media State . . . : Media disconnected\n' \
                   'Ethernet adapter Bluetooth Network Connection:\n\n   IPv4 Address. . . : 10.0.0.1\n'
        self.assertEqual(network_links(wlan, no_cable), dict(wifi='Home 5G', cable=False))
        self.assertEqual(network_links(wlan, cable), dict(wifi='Home 5G', cable=True))
        self.assertEqual(network_links('    State   : disconnected\n', cable), dict(wifi=None, cable=True))
        self.assertIn('by cable', link_warning(dict(wifi='Home 5G', cable=False), 'Virtual Desktop'))
        self.assertIn("Wi-Fi off", link_warning(dict(wifi='Home 5G', cable=True), 'Virtual Desktop'))
        self.assertIsNone(link_warning(dict(wifi=None, cable=True), 'Virtual Desktop'))
        self.assertIsNone(link_warning(dict(wifi='Home 5G', cable=False), 'Oculus'))
        # A Steam Frame's own network, which the PC joins for the headset's picture, is no Wi-Fi to turn off
        # (a Frame player's PC on October 13, on a cable too). A home Wi-Fi beside it still counts.
        frame = '    Name                   : Wi-Fi 2\n    State                  : connected\n' \
                '    SSID                   : frame_CBC1602B\n    BSSID                  : 02:00:00:00:00:01\n'
        self.assertEqual(network_links(frame, cable), dict(wifi=None, cable=True, headset_link='frame_CBC1602B'))
        self.assertIsNone(link_warning(network_links(frame, cable), 'SteamVR'))
        home = '    Name                   : Wi-Fi\n'+wlan
        self.assertEqual(network_links(frame+'\n'+home, no_cable),
                         dict(wifi='Home 5G', cable=False, headset_link='frame_CBC1602B'))
        self.assertIn('by cable', link_warning(network_links(home+'\n'+frame, no_cable), 'SteamVR'))
        # A Windows in another language: netsh's German word for encryption with the console's 0x81 for its
        # u-umlaut, which is a letter neither in UTF-8 (Python's under the launcher) nor in Windows' own code
        # page here. Read as either, the command left no output and 0.2.13 started no session on such a PC.
        german = command_text([sys.executable, '-c',
                               r"import sys; sys.stdout.buffer.write(b'    Verschl\x81sselung : CCMP\r\n')"])
        self.assertRegex(german, r'^    Verschl.sselung : CCMP\r?\n$')
        self.assertEqual(network_links(german, german), dict(wifi=None, cable=False))
        self.assertEqual(command_text(['a command no Windows has']), '')
        # xr_ms: frame, wait_frame, ..., end_frame. A held round is sampled twice; a slow copy is not one.
        held = [100.1, 6.9, .2, 0, 0, .1, .1, 0, 92.9]
        samples = [dict(frames=10, xr_ms=[13.9, 0, .2, 0, 0, .1, .1, 0, 13.2]), dict(frames=11, xr_ms=held),
                   dict(frames=11, xr_ms=held), dict(frames=12, xr_ms=held), dict(frames=13),
                   dict(frames=14, xr_ms=[95.0, 0, .2, 0, 0, 90.0, .1, 0, 4.0])]
        self.assertEqual(stream_stalls(samples), dict(rounds=2, seconds=.2))

    def test_eye_reflections_follow_the_games_setting(self):
        # The game's saved Screen Space Reflections value: 0 is Off; the eyes' buffers are made once.
        self.assertEqual([eye_reflections(value)[0] for value in (None, 0, 1, 2)], [True, False, True, True])
        self.assertIn('Turn it on', eye_reflections(0)[1])
        self.assertEqual(eye_reflections(1, refused=True)[0], False)
        self.assertEqual(eye_reflections(1, ray_traced=1)[0], False)
        self.assertIn('Ray-Traced', eye_reflections(1, ray_traced=1)[1])
        self.assertIn('--no-eye-reflections', eye_reflections(1, refused=True)[1])
        # A game that runs with Ray-Traced Reflections on keeps its own view rendering: resting, that view
        # hung the graphics device on the first frame it drew again. A session turns the setting off itself.
        self.assertEqual(ray_traced_view(0), (False, None))
        for value in (1, 2):
            keeps, line = ray_traced_view(value)
            self.assertTrue(keeps)
            self.assertIn('Ray-Traced Reflections is on', line)
        self.assertIn('Your setting returns', RAY_TRACING_OFF)
        # A sample's xr_ms follows the worker's stages, without its display period.
        self.assertEqual(XR_STAGES[:2]+XR_STAGES[-1:], ('frame', 'wait_frame', 'end_frame'))

    def test_the_render_scale_sizes_the_eyes(self):
        # core_tests.cpp's cases for spidy::scaledEyeSize: the console counts as the DLL does.
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
        # The console says what the size is of, and when the runtime took less.
        self.assertEqual(rendering_line(4608, 4896, 150, 0, [3072, 3264]),
                         "Rendering 4608 x 4896 pixels per eye (150% of the headset's 3072 x 3264), anti-aliased.")
        self.assertEqual(rendering_line(4000, 4248, 150, 0, [3072, 3264]),
                         "Rendering 4000 x 4248 pixels per eye (150% of the headset's 3072 x 3264 would be "
                         '4608 x 4896, more than the VR runtime takes), anti-aliased.')
        self.assertEqual(rendering_line(2048, 2048, 100, 2048, [2496, 2688]),
                         "Rendering 2048 x 2048 pixels per eye (a square size instead of the headset's), "
                         'anti-aliased.')
        self.assertEqual(rendering_line(3072, 3264, 100, 0, None),
                         'Rendering 3072 x 3264 pixels per eye, anti-aliased.')

    def test_upscaling_sizes_the_eyes_scene(self):
        # core_tests.cpp's cases for spidy::eyeRenderSize: the console counts as the DLL does. The October 10
        # measurement: 3072 x 3264 eyes at 67% rendered 2064 x 2192.
        self.assertEqual(eye_render_size(3072, 3264, 100), (3072, 3264))
        self.assertEqual(eye_render_size(3072, 3264, 67), (2064, 2192))
        self.assertEqual(eye_render_size(3072, 3264, 50), (1536, 1632))
        self.assertEqual(eye_render_size(2496, 2688, 77), (1928, 2072))
        self.assertEqual(eye_render_size(100, 100, 50), (64, 64))
        self.assertEqual(rendering_line(3072, 3264, 100, 0, [3072, 3264], 67),
                         "Rendering 3072 x 3264 pixels per eye (100% of the headset's 3072 x 3264): the scene at "
                         '2064 x 2192 (67% per side), anti-aliased and upscaled.')
        self.assertEqual(rendering_line(3072, 3264, 100, 0, [3072, 3264], 67, False),
                         "Rendering 3072 x 3264 pixels per eye (100% of the headset's 3072 x 3264), without "
                         'anti-aliasing.')

    def test_the_upscale_is_checked_before_anything_starts(self):
        import run_game_vr
        for argv in (['--upscale', '49'], ['--upscale', '101']):
            with patch.object(sys, 'argv', ['run_game_vr.py', *argv]), patch('sys.stderr'), \
                    patch.object(run_game_vr, 'session') as session, self.assertRaises(SystemExit):
                run_game_vr.main()
            session.assert_not_called()

    def test_the_monitor_rate_is_checked_before_anything_starts(self):
        import run_game_vr
        self.assertEqual((run_game_vr.MONITOR_RATES, run_game_vr.DEFAULT_MONITOR_RATE), ((0, 240), 30))
        for argv in (['--monitor-rate', '-1'], ['--monitor-rate', '241']):
            with patch.object(sys, 'argv', ['run_game_vr.py', *argv]), patch('sys.stderr'), \
                    patch.object(run_game_vr, 'session') as session, self.assertRaises(SystemExit):
                run_game_vr.main()
            session.assert_not_called()

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
        # Another build's render probe in the game: asked once, not for three minutes.
        from bridge_game import OtherBuild
        with patch.object(run_game_vr, 'alive', return_value=True), \
             patch.object(run_game_vr, 'discover_queue', side_effect=OtherBuild('older Spidy build')) as queue:
            with self.assertRaises(OtherBuild):
                run_game_vr.wait_for_renderer(game, 5, None)
        self.assertEqual(queue.call_count, 1)

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
                 patch.object(run_game_vr, 'other_build', return_value=None), \
                 patch.object(run_game_vr, 'had_vr', return_value=True) as had, \
                 patch.object(run_game_vr, 'preflight') as preflight:
                with self.assertRaisesRegex(run_game_vr.GameNeedsRestart, 'Close Spider-Man, then press START VR'):
                    run_game_vr.session(argparse.Namespace(stop_event=None, close_game=False), startup)
            had.assert_called_once()
            self.assertEqual(had.call_args[0][0], 7)
            preflight.assert_not_called()
            self.assertEqual(startup.stage, 'checking the running game')
            # The launcher offers to close the game for it (exit 6), and the session it then starts does.
            self.assertEqual(run_game_vr.exit_code(run_game_vr.GameNeedsRestart(run_game_vr.VR_RAN)), 6)

    def test_a_game_holding_another_spidy_build_is_named_and_can_be_closed_for_the_player(self):
        import argparse
        import hashlib
        import run_game_vr
        from bridge_game import OtherBuild, other_build_text, spidy_folder, staged
        from run_game_vr import other_build, restart_reason, Startup
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder).resolve()/'Spidy-0.3.8'
            (root/'build/windows-ninja').mkdir(parents=True)
            built = {}
            for name, _ in run_game_vr.SESSION_MODULES:
                (root/'build/windows-ninja'/name).write_bytes(name.encode())
                built[name] = hashlib.sha256(name.encode()).hexdigest()
            here = lambda name, staging, digest=None: dict(
                name=name, path=str(root/'reports'/staging/(digest or built[name])/name))
            game = [dict(name='Spider-Man.exe', path=r'C:\Games\Spider-Man.exe')]
            # This folder's build, staged where prepare puts it: nothing to say.
            ours = game+[here('spidy_render_memory.dll', 'render-memory-modules'),
                         here('spidy_bridge.dll', 'bridge-modules')]
            self.assertIsNone(other_build(7, lambda pid: ours, root))
            # The folder updated while the game ran: its new files hash otherwise. The same folder is named so.
            older = game+[here('spidy_render_memory.dll', 'render-memory-modules', '0'*64)]
            self.assertEqual(other_build(7, lambda pid: older, root), (True, root))
            # Another Spidy folder's module, flardy's October 16 case.
            elsewhere = pathlib.Path(folder).resolve()/'Spidy-0.3.6'
            other = game+[dict(name='SPIDY_RENDER_MEMORY.DLL', path=str(
                elsewhere/'reports/render-memory-modules'/built['spidy_render_memory.dll']/'spidy_render_memory.dll'))]
            self.assertEqual(other_build(7, lambda pid: other, root), (True, elsewhere))
            self.assertEqual(spidy_folder(r'C:\x\y.dll'), None)
            self.assertTrue(staged(here('spidy_bridge.dll', 'bridge-modules')['path'], root/'reports/bridge-modules',
                                   built['spidy_bridge.dll']))
            # A process that lists no modules says nothing.
            self.assertIsNone(other_build(7, Mock(side_effect=OSError('snapshot failed')), root))
            with patch.object(run_game_vr, 'ROOT', root), patch('bridge_game.ROOT', root):
                self.assertIn('older Spidy build in it: it was open while Spidy was updated', other_build_text(root))
                self.assertIn(f'Spidy from another folder in it ({elsewhere})', other_build_text(elsewhere))
                self.assertIn('another Spidy build in it', other_build_text(None))
                with patch.object(run_game_vr, 'other_build', side_effect=lambda pid, *_: (True, elsewhere)):
                    self.assertIn('another folder', restart_reason(7))
                # A refusal ends the session before the headset is looked for, with the launcher's exit code.
                startup = Startup(pathlib.Path(folder)/'game-vr-4.json')
                with patch.object(run_game_vr, 'game_running', return_value=True), \
                     patch.object(run_game_vr, 'find_game', return_value=7), \
                     patch.object(run_game_vr, 'other_build', return_value=(True, elsewhere)), \
                     patch.object(run_game_vr, 'preflight') as preflight:
                    with self.assertRaises(run_game_vr.GameNeedsRestart) as refused:
                        run_game_vr.session(argparse.Namespace(stop_event=None, close_game=False), startup)
                preflight.assert_not_called()
                self.assertEqual(run_game_vr.exit_code(refused.exception), 6)
                self.assertEqual(run_game_vr.exit_code(OtherBuild('x')), 6)
                # Asked to (--close-game): the game is closed, the other folder's saved settings go back, and the
                # session goes on to the headset.
                (elsewhere/'reports').mkdir(parents=True)
                with patch.object(run_game_vr, 'game_running', return_value=True), \
                     patch.object(run_game_vr, 'find_game', return_value=7), \
                     patch.object(run_game_vr, 'other_build', return_value=(True, elsewhere)), \
                     patch.object(run_game_vr, 'close_game', return_value=True) as closed, \
                     patch.object(run_game_vr.vr_display, 'restore') as restored, \
                     patch.object(run_game_vr, 'preflight', side_effect=RuntimeError('no headset')), \
                     patch('builtins.print'):
                    with self.assertRaisesRegex(RuntimeError, 'no headset'):
                        run_game_vr.session(argparse.Namespace(stop_event=None, close_game=True, xr_runtime='auto'), startup)
                closed.assert_called_once_with(7)
                self.assertEqual(restored.call_args.kwargs['backup'],
                                 elsewhere/'reports'/run_game_vr.vr_display.BACKUP.name)
                # A game that will not close is said so, and nothing else starts.
                with patch.object(run_game_vr, 'game_running', return_value=True), \
                     patch.object(run_game_vr, 'find_game', return_value=7), \
                     patch.object(run_game_vr, 'other_build', return_value=(True, elsewhere)), \
                     patch.object(run_game_vr, 'close_game', return_value=False), \
                     patch.object(run_game_vr, 'preflight') as preflight, patch('builtins.print'):
                    with self.assertRaisesRegex(RuntimeError, 'did not close within 30 seconds'):
                        run_game_vr.session(argparse.Namespace(stop_event=None, close_game=True, xr_runtime='auto'), startup)
                preflight.assert_not_called()

    def test_another_vr_mod_in_the_games_folder_ends_the_session_and_other_tools_are_named(self):
        import argparse
        import run_game_vr
        from run_game_vr import Startup
        # The game's own folder, as Steam installs it: nothing of it stands in for a Windows library.
        own = ('Spider-Man.exe', 'steam_api64.dll', 'd3dcompiler_47.dll', 'XAudio2_7.dll', 'amd_ags_x64.dll',
               'sl.interposer.dll', 'nvngx_dlss.dll', 'bink2w64.dll', 'cache.pso')
        with tempfile.TemporaryDirectory() as folder:
            game = pathlib.Path(folder)/"Marvel's Spider-Man Remastered"
            (game/'D3D12').mkdir(parents=True)
            for name in own:
                (game/name).write_bytes(b'')
            self.assertEqual(other_mods(game), ([], []))
            self.assertEqual(other_mods(game/'missing'), ([], []))
            # The mod's settings without its library load nothing.
            (game/'RealVR.ini').write_bytes(b'')
            self.assertEqual(other_mods(game), ([], []))
            # A player's folder of October 13: R.E.A.L. VR, whose dxgi.dll the game loaded before Windows'.
            for name in ('DXGI.dll', 'RealVR64.log', 'RealConfig.bat', 'openvr_api.dll'):
                (game/name).write_bytes(b'')
            (game/'RealRepo').mkdir()
            real, others = other_mods(game)
            self.assertEqual((real, others), (['DXGI.dll', 'RealVR.ini', 'RealVR64.log', 'RealConfig.bat', 'RealRepo'],
                                              []))
            refusal = real_vr_refusal(game, real)
            for said in ('R.E.A.L. VR', 'DXGI.dll, RealVR.ini', 'the headset stays black', str(game),
                         'press START VR'):
                self.assertIn(said, refusal)
            # The session ends there, before anything of Spidy's is in the game, and its report names the files.
            startup = Startup(pathlib.Path(folder)/'game-vr-4.json')
            running = Mock(pid=11, path=game/'Spider-Man.exe')
            with patch.object(run_game_vr, 'game_running', return_value=False), \
                 patch.object(run_game_vr, 'preflight', return_value=('runtime.json', dict(name='SteamVR'))), \
                 patch.object(run_game_vr, 'network_links', return_value={}), \
                 patch.object(run_game_vr, 'link_warning', return_value=None), \
                 patch.object(run_game_vr, 'wait_for_game', return_value=running), \
                 patch.object(run_game_vr, 'wait_for_renderer') as renderer:
                with self.assertRaisesRegex(RuntimeError, 'R.E.A.L. VR mod is in the game'):
                    run_game_vr.session(argparse.Namespace(stop_event=None, xr_runtime='auto', auto_launch=True),
                                        startup)
            renderer.assert_not_called()
            self.assertEqual((startup.stage, startup.fields['other_mods']), ("checking the game's folder", real))
            # Any other stand-in for a Windows library is named and the session goes on.
            for name in ('DXGI.dll', 'RealVR.ini', 'RealVR64.log', 'RealConfig.bat'):
                (game/name).unlink()
            (game/'RealRepo').rmdir()
            (game/'version.dll').write_bytes(b'')
            self.assertEqual(other_mods(game), ([], ['version.dll']))
            (game/'dinput8.dll').write_bytes(b'')
            self.assertEqual(other_mods(game), ([], ['dinput8.dll', 'version.dll']))
            warning = other_mods_warning(game, ['dinput8.dll', 'version.dll'])
            self.assertTrue(warning.startswith('WARNING: another mod or tool'))
            self.assertIn('move them out of', warning)
            self.assertIn('move it out of', other_mods_warning(game, ['version.dll']))
        self.assertIn('Your setting returns', ASPECT_RATIO_AUTO)

    def test_a_lost_graphics_device_is_named_from_the_game_log(self):
        # The October 16 player's game log, as the game wrote it (its dialog shows the same number).
        log = ('14:06:06:262 (00007064) > [Render] VMEM Available: 7275 MB\n'
               '14:06:06:262 (00007064) > [D3D] ERROR! Device removed detected (0x887A002B: UNKNOWN)\n')
        self.assertEqual(device_removed(log), '0x887A002B')
        self.assertEqual(device_removed(log.replace('0x887A002B', '0x887a0006')), '0x887A0006')
        self.assertIsNone(device_removed('[Render] Working set: 1 MB\n'))
        self.assertIsNone(device_removed(None))
        # 13 frames of VR play: as VR began; the name and what to try, the launcher's own option among it.
        early = device_lost_advice('0x887A002B', 13)
        self.assertIn('as VR play began', early)
        self.assertIn('DXGI_ERROR_ACCESS_DENIED', early)
        self.assertIn('Normal camera on the monitor: On', early)
        self.assertIn('#spidy-bugs', early)
        self.assertIn('in VR (0x887A0006, DXGI_ERROR_DEVICE_HUNG)', device_lost_advice('0x887A0006', 50000))
        self.assertIn('unknown', device_lost_advice('0x80004005', None))
        self.assertIn('Your setting returns', REFLEX_OFF)
        # The adapters the game listed, in its order: that player's one card twice.
        listed = ''.join(f'14:05:46:97{i} (00007064) > [Render] \tAdapter {i}:\r\n'
                         f'14:05:46:97{i} (00007064) > [Monitor] \t\tDescription             : {name}\r\n'
                         f'14:05:46:97{i} (00007064) > [Monitor] \t\tVendor                  : x\r\n'
                         for i, name in enumerate(('NVIDIA GeForce RTX 4070 SUPER', 'NVIDIA GeForce RTX 4070 SUPER',
                                                   'Microsoft Basic Render Driver')))
        self.assertEqual(game_adapters(listed), ['NVIDIA GeForce RTX 4070 SUPER', 'NVIDIA GeForce RTX 4070 SUPER',
                                                 'Microsoft Basic Render Driver'])
        self.assertEqual(game_adapters(''), [])

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
        for reader, magic, size in ((gpu_snapshot, 0x53475044, 208), (xr_snapshot, 0x53585244, 1176),
                                     (collision_snapshot, 0x53435044, 2160),
                                     (movement_snapshot, 0x534d5044, 57408),
                                     (motion_snapshot, 0x534d5644, 176), (ray_snapshot, 0x53525944, 784),
                                     (swing_snapshot,0x53574441,272),(timing_snapshot,0x5358544d,344),
                                     (appearance_snapshot,0x53415044,360),(eye_snapshot,0x53455353,112),
                                     (grab_snapshot,0x53475244,368),(body_snapshot,0x53424453,144),
                                     (punch_snapshot,0x53505544,168),(shooter_snapshot,0x53484f44,296),
                                     (strike_snapshot,0x53535452,328)):
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
        raw = bytearray(1176)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 39, 1176, 3, 4)
        self.assertIsNone(xr_snapshot(Reader(*([raw, struct.pack('<Q', 6)]*8)), 0))

    def test_xr_decodes_both_hands_and_status_message(self):
        raw = bytearray(1176)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 39, 1176, 3, 4)
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
        self.assertEqual(result['screen_3d_submitted'], 0)
        # A cutscene on that screen in stereo: 900 of its frames so far.
        struct.pack_into('<I', raw, 576, 4)
        struct.pack_into('<Q', raw, 808, 900)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['presentation'], result['screen_submitted'], result['screen_3d_submitted']),
                         ('game_screen_3d', 1234, 900))
        self.assertEqual((result['gate'], result['camera_mover']), ([], None))

    def test_xr_says_why_gameplay_was_unavailable_and_which_camera_ran(self):
        raw = bytearray(1176)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 39, 1176, 3, 4)
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

    def test_xr_counts_interacts_dodges_and_aim_markers(self):
        raw = bytearray(1176)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 39, 1176, 3, 4)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['interacts'], result['aim_markers'], result['markers']), (0, False, 0))
        self.assertEqual(result['dodges'], 0)
        # Three Y presses reached the game as its Y; markers on, 5000 drawn so far.
        struct.pack_into('<2IQ', raw, 648, 3, 1, 5000)
        # Seven B presses reached it as its B, the dodge, while VR showed play.
        struct.pack_into('<2I', raw, 576, 2, 7)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['interacts'], result['aim_markers'], result['markers']), (3, True, 5000))
        self.assertEqual(result['dodges'], 7)
        # Two of the Y presses were held on and walked the player up to the thing.
        self.assertEqual(result['interact_steps'], 0)
        struct.pack_into('<I', raw, 800, 2)
        self.assertEqual(xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)['interact_steps'], 2)
        # The right stick pushed down four times reached it as its D-pad down (heal), up once as its LT + RT.
        self.assertEqual((result['heals'], result['zips']), (0, 0))
        struct.pack_into('<2I', raw, 816, 4, 1)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['heals'], result['zips']), (4, 1))
        # Held up into the game's aim mode and let go: the zip went out again, and that is counted on its own.
        self.assertEqual(result['zip_releases'], 0)
        struct.pack_into('<2I', raw, 816, 4, 3)
        struct.pack_into('<I', raw, 852, 2)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['zips'], result['zip_releases']), (3, 2))
        # The game's own camera looks 37.5 degrees to the left of the head; the right stick has begun to turn it
        # nine times.
        self.assertEqual((result['look_error'], result['look_turns']), (0.0, 0))
        struct.pack_into('<fI', raw, 824, -37.5, 9)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['look_error'], result['look_turns']), (-37.5, 9))

    def test_xr_reports_the_vr_settings_and_the_settings_tab(self):
        raw = bytearray(1176)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 39, 1176, 3, 4)
        # Markers on; web grab, webs in open air, the web shooter and body on, punching off; 48 m/s, half again
        # real gravity, 45 degree snap turns, smooth turning at 120 degrees a second, half vibration, the large
        # screen; two changes in the SPIDY VR tab, which the game built 7 times.
        struct.pack_into('<2IQ', raw, 648, 0, 1, 0)
        struct.pack_into('<4IfIQ2I', raw, 664, 29, 45, 50, 2, 48.0, 2, 7, 1, 0)
        struct.pack_into('<I', raw, 704, 120)
        # Half again real gravity, the large HUD; flips at 300 degrees a second.
        struct.pack_into('<2I', raw, 744, 150, 3)
        struct.pack_into('<I', raw, 792, 300)
        # GAME SCREEN 3D: HIGH.
        struct.pack_into('<I', raw, 804, 3)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual(result['vr_settings'], dict(aim_markers=True, web_grab=True, air_webs=True, web_shooter=True,
                                                     punch=False, body=True, swing_speed=48.0, weight=150,
                                                     snap_turn=45, smooth_turn=120, haptics=50, screen_size=2,
                                                     flips=False, flip_speed=300, trigger_webs=False, hud=3,
                                                     stand_on_walls=False, screen_depth=3, arcade_webs=False,
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
        self.assertFalse(settings['flips'] or settings['air_webs'] or settings['stand_on_walls'])
        # STAND ON WALLS: walking a wall or stopped on it, the view turns onto it.
        struct.pack_into('<I', raw, 664, 5 | 128)
        settings = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)['vr_settings']
        self.assertTrue(settings['stand_on_walls'])
        self.assertFalse(settings['flips'] or settings['trigger_webs'])
        # Webs are always physical since October 15: no setting reports them.
        self.assertNotIn('physical_webs', settings)
        # WEB STYLE: ARCADE, webs through lamp posts and the like; nothing else changed.
        struct.pack_into('<I', raw, 664, 5 | 512)
        settings = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)['vr_settings']
        self.assertTrue(settings['arcade_webs'])
        self.assertFalse(settings['flips'] or settings['trigger_webs'] or settings['stand_on_walls'])
        # A trigger pulled at the temple scanned three times, and a held A charged two jumps.
        struct.pack_into('<2I', raw, 832, 3, 2)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['scans'], result['charge_jumps']), (3, 2))

    def test_xr_reports_the_layout_the_runtime_uses_for_the_controllers(self):
        raw = bytearray(1176)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 39, 1176, 3, 4)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        # Before the runtime says: nothing, and nothing in the session log.
        self.assertEqual((result['controllers'], result['controller_layouts']), ([None, None], []))
        self.assertIsNone(controllers_line(result['controllers']))
        # A Quest under SteamVR: Touch in both hands; the runtime took all three layouts.
        struct.pack_into('<3I', raw, 840, 1, 1, 2 | 4 | 8)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual(result['controllers'], ['touch', 'touch'])
        self.assertEqual(result['controller_layouts'], ['touch', 'index', 'steam_frame'])
        self.assertEqual(controllers_line(result['controllers']), 'Controllers: the Touch layout.')
        # Under SteamVR the menu button is SteamVR's dashboard: the log says how to pause instead.
        steamvr = controllers_line(result['controllers'], 'SteamVR')
        self.assertTrue(steamvr.startswith('Controllers: the Touch layout. SteamVR keeps the menu button'))
        self.assertIn('hold both thumbsticks in', steamvr)
        self.assertEqual(controllers_line(result['controllers'], 'Virtual Desktop'), 'Controllers: the Touch layout.')
        self.assertNotIn('SteamVR', controllers_line(['steam_frame', 'steam_frame'], 'SteamVR'))
        # A Steam Frame with its own layout, the right controller found first.
        struct.pack_into('<3I', raw, 840, 0, 3, 2 | 4 | 8)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual(result['controllers'], [None, 'steam_frame'])
        line = controllers_line(result['controllers'])
        self.assertTrue(line.startswith('Controllers: Steam Frame, with their own layout'))
        self.assertEqual(line, controllers_line(['steam_frame', 'steam_frame']))
        # Index controllers; a runtime that knows neither Index nor the Frame took Touch alone.
        struct.pack_into('<3I', raw, 840, 2, 2, 2)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['controllers'], result['controller_layouts']), (['index', 'index'], ['touch']))
        self.assertEqual(controllers_line(result['controllers']), 'Controllers: the Index layout.')
        # A layout of the runtime's own (none of Spidy's): the log says where to look.
        struct.pack_into('<3I', raw, 840, 4, 4, 2 | 4)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual(result['controllers'], ['runtime', 'runtime'])
        self.assertIn('a layout of its own', controllers_line(result['controllers']))

    def test_xr_reports_what_the_game_asked_a_player_for(self):
        raw = bytearray(1176)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 39, 1176, 3, 4)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        # Before any prompt: nothing asked for, and no line in the session log.
        self.assertEqual(result['prompts']['asking'], None)
        self.assertEqual(result['prompts']['last_asked'], None)
        self.assertIsNone(prompts_line(0))
        # The hint line of the Shocker fight: the game's LB and RB, held.
        pull = 0x100 | 0x200 | 1 << 18
        struct.pack_into('<6Q', raw, 856, 4000, 12, 9, 3, 7, 6)
        struct.pack_into('<4I', raw, 904, pull, pull, 5, 2)
        struct.pack_into('<4I', raw, 920, 4, 1, 1, 0)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        prompts = result['prompts']
        self.assertEqual((prompts['invokes'], prompts['calls']), (4000, 12))
        self.assertEqual((prompts['raised'], prompts['cleared']), (9, 3))
        self.assertEqual((prompts['texts'], prompts['rewrites']), (7, 6))
        self.assertEqual(prompts['asking'], 'LB + RB (hold)')
        self.assertEqual(prompts['last_asked'], 'LB + RB (hold)')
        self.assertEqual((prompts['beyond_play'], prompts['keyboard_texts']), (5, 2))
        self.assertEqual((prompts['context_presses'], prompts['pad_entries']), (4, 1))
        self.assertTrue(prompts['pad_layer'])
        # Both bumpers together is a control VR play has: the log says so plainly.
        self.assertTrue(reachable_in_play(pull))
        self.assertIn('The game is asking for LB + RB (hold)', prompts_line(pull))
        self.assertNotIn('no button of its own', prompts_line(pull))
        # The game's X alone, and each bumper alone, are what play never had.
        for word, name in ((0x4000, 'X'), (0x100, 'LB'), (1 << 16, 'LT'), (0x1, 'D-pad up'), (0x40, 'L3')):
            self.assertEqual(demand_name(word), name)
            self.assertFalse(reachable_in_play(word))
            self.assertIn('no button of its own', prompts_line(word))
        # A vent's LT and RT together, and a stick-motion QTE.
        self.assertEqual(demand_name(3 << 16), 'LT + RT')
        self.assertTrue(reachable_in_play(3 << 16))
        self.assertEqual(demand_name(2 << 21), 'left stick forward')
        self.assertTrue(reachable_in_play(2 << 21))
        self.assertEqual(demand_name(1 << 23), 'right stick left')
        self.assertFalse(reachable_in_play(1 << 23))

    def test_every_shared_struct_agrees_between_the_modules_and_the_tools(self):
        """The C++ struct and the Python reader must name the same version and size.

        Nothing checked this before, and on October 13 it cost three sessions: the census raised
        PromptData's size from 3200 to 10056 and this reader's expected version from 1 to 2, but the
        struct in game_xr.cpp still said version 1. Every protocol check passed - they test the reader
        against bytes the test itself packs - while the module and the tool disagreed, so the reader
        raised on its first pass, the session loop died, and the game dropped out of VR to the flat
        screen. This reads both sides instead."""
        root = pathlib.Path(__file__).resolve().parent.parent
        # The modules: `uint32_t magic = 0x..., version = N, bytes = sizeof(Struct)` with the
        # `static_assert(sizeof(Struct) == SIZE)` that pins its size.
        declared, sizes = {}, {}
        for source in list((root/'src').glob('*.cpp')) + list((root/'include'/'spidy').glob('*.hpp')):
            text = source.read_text(encoding='utf-8', errors='replace')
            for magic, version, name in re.findall(
                    r'magic\s*=\s*(0x[0-9a-fA-F]+)\s*,\s*version\s*=\s*(\d+)\s*,'
                    r'\s*bytes\s*=\s*sizeof\((\w+)\)', text):
                declared[int(magic, 16)] = (name, int(version), source.name)
            # Keyed by file as well: several of these structs are named `Data`, in
            # game_swing, web_strike, native_hud and more.
            for name, size in re.findall(r'static_assert\(sizeof\((\w+)\)\s*==\s*(\d+)\)', text):
                sizes[(source.name, name)] = int(size)
        self.assertIn(0x53585244, declared, 'XrData was not found in the modules')
        self.assertIn(0x53505244, declared, 'PromptData was not found in the modules')
        # The tools: every `(magic, version, size)` triple a reader checks against.
        tool = (root/'tools'/'run_game_vr.py').read_text(encoding='utf-8', errors='replace')
        wanted = {int(m, 16): (int(v), int(s))
                  for m, v, s in re.findall(r'\((0x[0-9a-fA-F]+),\s*(\d+),\s*(\d+)\)', tool)}
        self.assertIn(0x53585244, wanted, 'no reader checks XrData')
        checked = 0
        for magic, (version, size) in sorted(wanted.items()):
            if magic not in declared:
                continue  # a struct another module owns; its own suite covers it
            name, module_version, source = declared[magic]
            self.assertEqual(module_version, version,
                             f'{name} is version {module_version} in {source} and {version} in the tools')
            if (source, name) in sizes:
                self.assertEqual(sizes[(source, name)], size,
                                 f'{name} is {sizes[(source, name)]} bytes in {source} '
                                 f'and {size} in the tools')
            checked += 1
        # Both of the structs this file reads, at least, must have been compared.
        self.assertGreaterEqual(checked, 2, 'the version check compared almost nothing')

    def test_xr_reports_the_controller_buttons_the_player_pressed(self):
        raw = bytearray(1176)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 39, 1176, 3, 4)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['vr_buttons'], result['vr_buttons_played']), ([], []))
        # The October 13 question: B arrived and Y never did, so which hand was pressed?
        struct.pack_into('<2I', raw, 936, 2, 2 | 8 | 16)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual(result['vr_buttons'], ['b'])
        self.assertEqual(result['vr_buttons_played'], ['b', 'y', 'menu'])
        # Y pressed but never reaching the game is a different fault from Y never pressed.
        self.assertEqual(controller_buttons(0), [])
        self.assertEqual(controller_buttons(8), ['y'])
        self.assertEqual(controller_buttons(1 | 512), ['a', 'dpad_right'])

    def test_the_prompt_calls_reach_the_report_with_their_arguments(self):
        raw = bytearray(12112)
        struct.pack_into('<4IQ', raw, 0, 0x53505244, 4, 12112, 0, 4)
        # The October 13 session's shape: the names matched, no text did.
        struct.pack_into('<6Q', raw, 24, 630, 107, 107, 0, 0, 0)
        struct.pack_into('<4I', raw, 72, 0, 0, 0, 0)
        struct.pack_into('<2I', raw, 88, 2, 0)
        struct.pack_into('<10I', raw, 96, 0, 0, 0, 0, 105, 2, 0, 0, 0, 0)
        # The timeline: the game's calls and this session's presses, in order.
        for i, line in enumerate((b" 12100 POIInteractAvailable(3, 1, '')",
                                  b" 12340 >> the player's Y -> the game, on a wall")):
            raw[136+i*128:136+i*128+len(line)] = line
        # The census: every name the UI called, with one example of its arguments.
        struct.pack_into('<2I', raw, 4232, 3, 0)
        struct.pack_into('<3I', raw, 4240, 105, 2, 418)
        for i, line in enumerate((b"POIInteractAvailable(3, 1, '')", b'AddQTE(2, 1, 0, 0)',
                                  b"ShowText(7, 'hold [BTN_B] to whip around a corner.')")):
            raw[4432+i*160:4432+i*160+len(line)] = line
        result = prompt_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual((result['invokes'], result['calls']), (630, 107))
        self.assertEqual((result['raised'], result['cleared']), (107, 0))
        self.assertEqual((result['texts'], result['rewrites']), (0, 0))
        self.assertIsNone(result['asking'])
        # Only the names that came are listed, so the report says which fire.
        self.assertEqual(result['by_name'], {'POIInteractAvailable': 105, 'AddQTE': 2})
        # The timeline keeps the order, and marks this session's own lines with '>>'.
        self.assertEqual(result['timeline'], [" 12100 POIInteractAvailable(3, 1, '')",
                                              " 12340 >> the player's Y -> the game, on a wall"])
        # The census is ordered by how often each name came, busiest first, so a
        # call carrying a prompt's text shows up however this reader was written.
        self.assertEqual([c['call'] for c in result['census']],
                         ["ShowText(7, 'hold [BTN_B] to whip around a corner.')",
                          "POIInteractAvailable(3, 1, '')", 'AddQTE(2, 1, 0, 0)'])
        self.assertEqual([c['times'] for c in result['census']], [418, 105, 2])
        # A version this reader does not know is reported, never raised: these are
        # diagnostics, and raising ended three sessions on October 13 (the module said
        # version 1, this reader wanted 2) by dropping the player out of VR.
        struct.pack_into('<I', raw, 4, 5)
        mismatch = prompt_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual(mismatch['protocol_mismatch']['version'], 5)
        self.assertEqual(mismatch['protocol_mismatch']['wanted']['version'], 4)
        self.assertNotIn('census', mismatch)

    def test_xr_reports_walls_the_game_held_the_player_on(self):
        raw = bytearray(1176)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 39, 1176, 3, 4)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual(result['surface'], dict(entries=0, frames=0, hero_up=[0, 0, 0], stand_off_m=0,
                                                 head_height_m=0, head_clearance_m=0, view_tilt_deg=0))
        # Standing on a wall: the view a quarter turn from the world's up.
        struct.pack_into('<f', raw, 796, 90.0)
        self.assertEqual(xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)['surface']['view_tilt_deg'], 90.0)
        struct.pack_into('<f', raw, 796, 0.0)
        # Two stretches on a wall, 450 frames in all; the player's up along the wall's normal (+x); the head
        # 0.48 m off it, placed from the feet it would have been 0.02 m inside it.
        struct.pack_into('<IQ3f3f', raw, 708, 2, 450, 1, 0, 0, .48, -.02, .46)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual(result['surface'], dict(entries=2, frames=450, hero_up=[1, 0, 0], stand_off_m=.48,
                                                 head_height_m=-.02, head_clearance_m=.46, view_tilt_deg=0))

    def test_xr_reports_the_game_s_own_crawl_under_the_player(self):
        raw = bytearray(1176)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 39, 1176, 3, 4)
        self.assertEqual(xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)['warp'],
                         dict(asked=0, applied=0, refused=0, faulted=0, taken=0, declined=0, game_run_frames=0,
                              hold_grace_frames=0))
        # Three rests on the swing's wall asked for the crawl; the game took all three requests, its crawl took
        # him twice and the swing had him back once; 240 frames of the game's own wall run, 12 of them the view
        # held its wall through a gap in the swing's reports.
        struct.pack_into('<8I', raw, 1008, 3, 3, 0, 0, 2, 1, 240, 12)
        self.assertEqual(xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)['warp'],
                         dict(asked=3, applied=3, refused=0, faulted=0, taken=2, declined=1, game_run_frames=240,
                              hold_grace_frames=12))

    def test_xr_reports_gaps_in_play_the_vent_crawl_and_the_game_camera(self):
        raw = bytearray(1176)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 39, 1176, 3, 4)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual(result['tracking_gaps'], dict(frames=0, reasons=[]))
        self.assertEqual(result['floor_crawl'], dict(frames=0, capped_frames=0, kneel_frames=0))
        self.assertEqual(result['game_camera'], dict(forward=[0, 0, 0], up=[0, 0, 0]))
        # Three frames of play not placed: one the game camera looked straight up a shaft before the start,
        # two the headset repeated a display time. Then 600 frames crawling a duct's floor, 540 of them with
        # the head held at the crawl's height and 500 kneeling; the camera looking up the shaft, its up out.
        struct.pack_into('<5I', raw, 1040, 3, 16 | 64, 600, 540, 500)
        struct.pack_into('<6f', raw, 1064, .12, .985, .12, -.696, .174, -.696)
        result = xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)
        self.assertEqual(result['tracking_gaps'], dict(frames=3, reasons=['no_camera', 'stale_display']))
        self.assertEqual(result['floor_crawl'], dict(frames=600, capped_frames=540, kneel_frames=500))
        self.assertEqual(result['game_camera'], dict(forward=[.12, .985, .12], up=[-.696, .174, -.696]))

    def test_xr_reports_pointing_at_the_game_s_interactions(self):
        raw = bytearray(1176)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 39, 1176, 3, 4)
        self.assertEqual(xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)['offers'],
                         dict(hooks='off', hook_error=0, point_frames=0, reach_frames=0, asks=0, refused=0,
                              presses=0, picked=0, failed=0, most_seen=0, masked_updates=0, posed_checks=0,
                              gathers=0, most_checked=0, spot_searches=0, spots_found=0, near_now=0,
                              press_retries=0))
        # 300 frames pointed at a vent the player could use and 120 at one to come closer to; three presses
        # asked for it and a fourth the game refused at once; two came to its interact (one of them picked by
        # the game first) and one was given up; four offers near him at most.
        struct.pack_into('<8I', raw, 1088, 300, 120, 3, 1, 2, 1, 1, 4)
        struct.pack_into('<I', raw, 1060, 1)
        # The module: 2000 updates with the crawl's mask, 2600 checks against its pose, 1900 gathers that
        # checked something, three offers at most; four spot searches, three found; two offers near him now.
        struct.pack_into('<8I', raw, 1120, 2000, 2600, 1900, 3, 4, 3, 2, 1)
        self.assertEqual(xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)['offers'],
                         dict(hooks='hooked', hook_error=0, point_frames=300, reach_frames=120, asks=3, refused=1,
                              presses=2, picked=1, failed=1, most_seen=4, masked_updates=2000, posed_checks=2600,
                              gathers=1900, most_checked=3, spot_searches=4, spots_found=3, near_now=2,
                              press_retries=1))
        # On a game build whose functions are not these, the hooks stay out and say why.
        struct.pack_into('<I', raw, 1060, 3 | 9601 << 8)
        self.assertEqual({k: v for k, v in xr_snapshot(Reader(raw, struct.pack('<Q', 4)), 0)['offers'].items()
                          if k in ('hooks', 'hook_error')}, dict(hooks='failed', hook_error=9601))

    def test_xr_reports_the_t_pose_calibration(self):
        raw = bytearray(1176)
        struct.pack_into('<4IQ', raw, 0, 0x53585244, 39, 1176, 3, 4)
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
                    arm_length=0, no_calibration_prompt=False, no_stand_on_walls=False, screen_depth=2,
                    arcade_webs=False)
        start = start_settings(args)
        self.assertEqual(start, dict(aim_markers=True, web_grab=True, air_webs=True, web_shooter=True, punch=False,
                                     body=True, swing_speed=32.0, weight=60, snap_turn=30, smooth_turn=0,
                                     haptics=100, screen_size=1, flips=False, flip_speed=180, trigger_webs=False,
                                     hud=2, stand_on_walls=True, screen_depth=2, arcade_webs=False,
                                     eye_height_mm=0, arm_length_mm=0, calibration_prompt=True))
        # Nothing changed, or no sample: no line.
        self.assertIsNone(settings_line(start, dict(vr_settings=dict(start))))
        self.assertIsNone(settings_line(start, None))
        changed = dict(start, air_webs=False, web_shooter=False, punch=True, swing_speed=48.0, weight=150,
                       snap_turn=0, smooth_turn=90)
        self.assertEqual(settings_line(start, dict(vr_settings=changed)),
                         SETTINGS_LINE + 'aim_markers=1 web_grab=1 air_webs=0 web_shooter=0 punch=1 body=1 '
                                         'swing_speed=48 weight=150 snap_turn=0 smooth_turn=90 haptics=100 '
                                         'screen_size=1 flips=0 flip_speed=180 trigger_webs=0 hud=2 '
                                         'stand_on_walls=1 screen_depth=2 arcade_webs=0 eye_height_mm=0 '
                                         'arm_length_mm=0 calibration_prompt=1')
        # The experimental flips switched on in the headset go to the launcher too; so do their speed, the web
        # button and the HUD.
        self.assertIn(' flips=1 ', settings_line(start, dict(vr_settings=dict(start, flips=True))))
        self.assertIn(' flip_speed=240 ', settings_line(start, dict(vr_settings=dict(start, flip_speed=240))))
        self.assertIn(' trigger_webs=1 ', settings_line(start, dict(vr_settings=dict(start, trigger_webs=True))))
        self.assertIn(' hud=0 ', settings_line(start, dict(vr_settings=dict(start, hud=0))))
        self.assertIn(' stand_on_walls=0 ', settings_line(start, dict(vr_settings=dict(start, stand_on_walls=False))))
        # GAME SCREEN 3D switched off in the headset: the next session's cutscenes are flat again.
        self.assertIn(' screen_depth=0 ', settings_line(start, dict(vr_settings=dict(start, screen_depth=0))))
        # WEB STYLE: ARCADE chosen in the headset stays for the next session.
        self.assertIn(' arcade_webs=1 ', settings_line(start, dict(vr_settings=dict(start, arcade_webs=True))))
        # A T-pose calibration in the headset goes to the launcher the same way; so does skipping it.
        self.assertTrue(settings_line(start, dict(vr_settings=dict(start, eye_height_mm=1630, arm_length_mm=590)))
                        .endswith(' eye_height_mm=1630 arm_length_mm=590 calibration_prompt=1'))
        self.assertTrue(settings_line(start, dict(vr_settings=dict(start, calibration_prompt=False)))
                        .endswith(' calibration_prompt=0'))


if __name__ == '__main__':
    unittest.main()

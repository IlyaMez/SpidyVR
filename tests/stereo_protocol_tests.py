"""Check eye telemetry boundaries and reject torn process-memory snapshots."""
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from probe_stereo import snapshot, frame_snapshot, motion_command, render_memory_snapshot


def payload(sequence=2, version=4):
    raw = bytearray(352)
    struct.pack_into('<4IQ', raw, 0, 0x53535042, version, 352, 2, sequence)
    for i in range(2):
        offset = 64 + i * 144
        struct.pack_into('<4Q4I', raw, offset, 0x10000+i*0x20000, 0, 0, 45, 17, 512, 512, 0)
        struct.pack_into('<Q2I', raw, offset+112, 0x90000+i*0x100000, 512*512*4, 12+i)
        struct.pack_into('<4I', raw, offset+128, 3072,3264,3072,3264)
    return bytes(raw)


class Reader:
    def __init__(self, values):
        self.values = iter(values)

    def read(self, address, size):
        return next(self.values)


class StereoProtocolTests(unittest.TestCase):
    def test_both_eye_readbacks_keep_their_identity(self):
        value = snapshot(Reader([payload(), struct.pack('<Q', 2)]), 0)
        self.assertEqual([e['readback'] for e in value['eyes']], [0x90000, 0x190000])
        self.assertEqual([e['readback_completed'] for e in value['eyes']], [12, 13])
        self.assertEqual([e['view'] for e in value['eyes']], [0x10000, 0x30000])
        self.assertEqual([(e['render_width'],e['render_height']) for e in value['eyes']], [(3072,3264)]*2)

    def test_eye_job_frames_report_reclaimed_copies_and_reject_old_layouts(self):
        raw = bytearray(248)
        struct.pack_into('<4IQ', raw, 0, 0x53455044, 4, 248, 0, 6)
        struct.pack_into('<25Q', raw, 24, *range(1, 17), 9, 21, 22, 31, 32, 41, 42, 51, 52)
        struct.pack_into('<Q3f', raw, 224, 61, .5, -.25, 2.0)
        value = frame_snapshot(Reader([raw, struct.pack('<Q', 6)]), 0)
        self.assertEqual((value['error'], value['left_begins'], value['right_ends'], value['reclaimed']),
                         (0, 5, 8, 9))
        self.assertEqual((value['left_history_moved'], value['right_history_moved'], value['left_history_still'],
                          value['right_history_still']), (21, 22, 31, 32))
        self.assertEqual((value['left_same_frame_poses'], value['right_same_frame_poses'],
                          value['left_late_frame_poses'], value['right_late_frame_poses']), (41, 42, 51, 52))
        self.assertEqual((value['rope_frames'], value['rope_from_eye']), (61, (.5, -.25, 2.0)))
        struct.pack_into('<Q', raw, 16, 7)
        self.assertIsNone(frame_snapshot(Reader([raw, struct.pack('<Q', 7)]*8), 0))
        old = bytearray(160)
        struct.pack_into('<4IQ', old, 0, 0x53455044, 2, 160, 0, 2)
        self.assertIsNone(frame_snapshot(Reader([old, struct.pack('<Q', 2)]), 0))
        struct.pack_into('<3I', raw, 0, 0x53455044, 3, 248)
        with self.assertRaisesRegex(RuntimeError, 'protocol mismatch'):
            frame_snapshot(Reader([raw, struct.pack('<Q', 6)]), 0)

    def test_render_memory_reports_megabytes_and_which_ring_the_game_is_on(self):
        # native_render_memory::Data: status at 12, sequence at 16, frame counts at 24, kilobytes from 40.
        raw = bytearray(64)
        struct.pack_into('<4Iq2Q6I', raw, 0, 0x53524d44, 1, 64, 3, 8, 1200, 13, 131072, 524288, 61440, 65536,
                         124518, 0)
        value = render_memory_snapshot(Reader([raw, struct.pack('<q', 8)]), 0)
        self.assertEqual(value, dict(ring='spidy_ring', frames=1200, overflow_frames=13, game_ring_mb=128,
                                     ring_mb=512, last_frame_mb=60, worst_frame_mb=64,
                                     worst_two_frames_mb=124518/1024, error=0))
        for status, ring in ((0, 'off'), (1, 'not_created'), (2, 'game_ring'), (9, 9)):
            struct.pack_into('<I', raw, 12, status)
            self.assertEqual(render_memory_snapshot(Reader([raw, struct.pack('<q', 8)]), 0)['ring'], ring)
        # A write in progress (odd sequence) or between the two reads is read again, eight times at most.
        newer = bytearray(raw)
        struct.pack_into('<q', newer, 16, 10)
        struct.pack_into('<Q', newer, 24, 1201)
        value = render_memory_snapshot(Reader([raw, struct.pack('<q', 10), newer, struct.pack('<q', 10)]), 0)
        self.assertEqual(value['frames'], 1201)
        struct.pack_into('<q', raw, 16, 9)
        self.assertIsNone(render_memory_snapshot(Reader([raw, struct.pack('<q', 9)]*8), 0))
        self.assertIsNone(render_memory_snapshot(Reader([raw[:40], struct.pack('<q', 9)]), 0))
        struct.pack_into('<3I', raw, 0, 0x53524d44, 2, 64)
        with self.assertRaisesRegex(RuntimeError, 'protocol mismatch'):
            render_memory_snapshot(Reader([raw, struct.pack('<q', 8)]), 0)

    def test_diagnostic_eye_command_matches_the_tracked_eye_protocol(self):
        main = (1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 10, 20, 30, 1)
        packet = motion_command(main, 7, 0, travel=2)
        # native_eyes::Command v2: 208 bytes, eye poses at 32 and 112, anchor at 192.
        self.assertEqual(len(packet), 208)
        self.assertEqual(struct.unpack_from('<4IQ2I', packet), (0x53455043, 2, 208, 1, 7, 250, 0))
        left, right = struct.unpack_from('<16f', packet, 32), struct.unpack_from('<16f', packet, 112)
        self.assertAlmostEqual(right[12]-left[12], .064, places=5)
        self.assertAlmostEqual((left[12]+right[12])/2, 12, places=5)
        self.assertEqual(struct.unpack_from('<3fI', packet, 192), (0, 0, 0, 0))

    def test_eye_frame_check_requires_same_frame_poses_and_a_moving_history(self):
        from probe_eye_frames import assess, phase_counts
        before = {f'left_{name}': 100 for name in ('copies', 'same_frame_poses', 'late_frame_poses',
                                                    'history_moved', 'history_still')}
        after = dict(before, left_copies=500, left_late_frame_poses=499, left_same_frame_poses=101,
                     left_history_still=500)
        late = phase_counts(before, after, 120)
        self.assertEqual(late, dict(copies=400, same_frame_poses=1, late_frame_poses=399, history_moved=0,
                                    history_still=400, commands=120))
        frame = dict(copies=400, same_frame_poses=399, late_frame_poses=1, history_moved=118, history_still=282,
                     commands=120)
        self.assertTrue(assess(late, frame)['passed'])
        # The fix under test: poses still a frame late, or a history that never moves, must fail.
        self.assertFalse(assess(late, dict(frame, late_frame_poses=399, same_frame_poses=1))['passed'])
        self.assertFalse(assess(late, dict(frame, history_moved=0))['passed'])
        # The comparison phase must show the old behaviour, or the probe measured nothing.
        self.assertFalse(assess(frame, frame)['passed'])
        self.assertFalse(assess(late, dict(frame, copies=5))['passed'])

    def test_web_frame_probe_places_the_web_start_in_front_of_a_sideways_camera(self):
        import math
        from probe_web_frames import rig, eye_command, web_command, expected_pixel, camera, mark
        heading = (.6, 0., .8)
        right, down, forward = camera(heading)
        # Native rows are right, down, forward: right x down must give forward.
        cross = (right[1]*down[2]-right[2]*down[1], right[2]*down[0]-right[0]*down[2],
                 right[0]*down[1]-right[1]*down[0])
        for a, b in zip(cross, forward):
            self.assertAlmostEqual(a, b)
        self.assertAlmostEqual(sum(a*b for a, b in zip(forward, heading)), 0)
        eyes, wrist, target, expected = rig((100., 50., -20.), heading)
        left, right_eye = eyes
        self.assertAlmostEqual(math.dist(left[12:15], right_eye[12:15]), .064)
        # The web start, as the game reports it, is relative to the left eye.
        for a, b, e in zip(wrist, left[12:15], expected):
            self.assertAlmostEqual(a-b, e)
        self.assertAlmostEqual(wrist[1], 50+1.6-.15)
        packet = eye_command(9, eyes, (100., 50., -20.))
        self.assertEqual(len(packet), 208)
        self.assertEqual(struct.unpack_from('<4IQ2I', packet), (0x53455043, 2, 208, 1, 9, 250, 0))
        self.assertEqual(struct.unpack_from('<3fI', packet, 192), (100, 50, -20, 1))
        fov = struct.unpack_from('<4f', packet, 96)
        self.assertTrue(fov[0] < 0 < fov[1] and fov[2] < 0 < fov[3])
        packet = web_command((100., 50., -20.), wrist, target)
        # native_webs::ProbeCommand: header 16, feet at 16, hands at 32 and 72 (attached, tracked, attachedAt, anchor, wrist).
        self.assertEqual(len(packet), 112)
        self.assertEqual(struct.unpack_from('<4I3f', packet), (0x5357424d, 1, 112, 200, 100, 50, -20))
        attached, tracked, attached_at, *points = struct.unpack_from('<2Iq6f', packet, 32)
        self.assertEqual((attached, tracked, attached_at), (1, 1, 1))
        for a, b in zip(points, (*target, *wrist)):
            self.assertAlmostEqual(a, b, places=4)
        self.assertEqual(struct.unpack_from('<2Iq', packet, 72), (0, 0, 0))
        x, y = expected_pixel(512)
        self.assertAlmostEqual(x, 256+.032/.6*256, places=4)
        self.assertAlmostEqual(y, 256+.15/.6*256, places=4)
        marked = mark(bytes(3*8*8), 8, 4, 4, arm=3, gap=2)
        self.assertEqual(marked[(4*8+4)*3:(4*8+4)*3+3], bytes(3))  # centre stays open
        self.assertEqual(marked[(4*8+6)*3:(4*8+6)*3+3], bytes((0, 255, 0)))

    def test_web_frame_probe_requires_a_frame_of_lag_before_and_none_after(self):
        from probe_web_frames import summarize, assess
        late = [dict(speed=20., dt=.005, miss=.1, late=True)]*20+[dict(speed=2., dt=.005, miss=.01, late=True)]
        frame = [dict(speed=24., dt=.005, miss=.002, late=False)]*20
        late_summary, frame_summary = summarize(late), summarize(frame)
        self.assertEqual(late_summary['samples'], 20)  # slow samples say nothing about travel
        self.assertAlmostEqual(late_summary['mean_frame_travel_m'], .1)
        self.assertTrue(assess(late_summary, frame_summary)['passed'])
        # The web still a frame ahead of the hand with the new placement must fail.
        self.assertFalse(assess(late_summary, summarize([dict(speed=24., dt=.005, miss=.12, late=False)]*20))['passed'])
        # Without a late phase that shows the lag, the probe has not measured anything.
        self.assertFalse(assess(frame_summary, frame_summary)['passed'])
        self.assertFalse(assess(summarize([]), frame_summary)['passed'])

    def test_exposure_probe_compares_what_both_eyes_see_and_requires_alike_shared_images(self):
        import math
        from probe_exposure import head_eyes, shared_columns, block_light, compare_light, assess, view_light
        from probe_hud import INNER, OUTER
        for roll in (0, 90, -90, 30):
            (left, left_fov), (right, _) = head_eyes((100., 50., -20.), (.6, .8), math.radians(10), math.radians(roll))
            for m in (left, right):
                right_axis, down, forward = m[0:3], m[4:7], m[8:11]
                # Native rows are right, down, forward: right x down must give forward.
                cross = (right_axis[1]*down[2]-right_axis[2]*down[1], right_axis[2]*down[0]-right_axis[0]*down[2],
                         right_axis[0]*down[1]-right_axis[1]*down[0])
                for a, b in zip(cross, forward):
                    self.assertAlmostEqual(a, b)
            self.assertAlmostEqual(math.dist(left[12:15], right[12:15]), .064)
            # Rolled to its right the head's right eye is the lower one: straight below the left at 90.
            self.assertAlmostEqual(left[13]-right[13], .064*math.sin(math.radians(roll))*math.cos(math.radians(10)))
            self.assertGreater(-left_fov[0], left_fov[1])
        # The lenses overlap between the inner edges: the left image's last columns are the right's first.
        width, height = 480, 96
        columns = shared_columns(width)
        self.assertAlmostEqual(columns/width, 2*math.tan(INNER)/(math.tan(INNER)+math.tan(OUTER)), places=2)

        def image(shift, gain):
            """A far scene, brighter to the right, as an eye whose lens starts `shift` columns into it draws it."""
            rows = bytearray()
            for y in range(height):
                for x in range(width):
                    value = min(255, int((40+(x+shift)//4+y//2)*gain))
                    rows += bytes((value, value, value, 255))
            return bytes(rows)
        left_image = image(0, 1)
        same = compare_light(block_light(left_image, width, height, width-columns, columns),
                             block_light(image(width-columns, 1), width, height, 0, columns))
        self.assertEqual((same['median'], same['total'], same['tenths']), (1, 1, [1, 1]))
        self.assertGreater(same['blocks'], 500)
        darker = compare_light(block_light(left_image, width, height, width-columns, columns),
                               block_light(image(width-columns, .8), width, height, 0, columns))
        # sRGB values 0.8 times as high are about 0.6 times the light.
        self.assertAlmostEqual(darker['median'], 1/.8**2.3, delta=.12)
        # Black and white say nothing about exposure.
        black = block_light(bytes(width*height*4), width, height, 0, columns)
        self.assertEqual(compare_light(black, black), dict(blocks=0, median=None, total=None, tenths=None))

        def capture(own, shared, own_jobs=0, jobs=140):
            return dict(images=dict(own=dict(median=own), shared=dict(median=shared)),
                        shared_jobs=dict(own=own_jobs, shared=jobs))
        self.assertEqual(assess(dict(level=capture(1.0, 1.0), wall=capture(1.31, 1.01)), .03),
                         dict(measured=True, switched=True, shared_alike=True, reproduced=True, passed=True))
        # The fix under test: eyes that still differ with the shared exposure must fail.
        self.assertFalse(assess(dict(wall=capture(1.31, 1.2)), .03)['passed'])
        self.assertFalse(assess(dict(wall=capture(1.31, .9)), .03)['passed'])
        # A view that is alike on both sides shows no old difference, which is not a failure.
        level = assess(dict(level=capture(1.01, 1.0)), .03)
        self.assertEqual((level['reproduced'], level['passed']), (False, True))
        # The shared phase must have shared, and the comparison phase must not have.
        self.assertFalse(assess(dict(wall=capture(1.31, 1.0, jobs=0)), .03)['passed'])
        self.assertFalse(assess(dict(wall=capture(1.0, 1.0, own_jobs=300)), .03)['passed'])
        self.assertFalse(assess(dict(wall=capture(None, None)), .03)['passed'])
        self.assertFalse(assess({}, .03)['passed'])
        # A view's luminance: in use at +1708 and +170c, its own newest reading at +1720 and +1724.
        raw = struct.pack('<2f2Q2f', .5, .25, 0x1111, 0x2222, .75, .125)
        self.assertEqual(view_light(Reader([raw]), 0x1000),
                         dict(adapted=.5, measured=.25, own_adapted=.75, own_measured=.125))
        self.assertIsNone(view_light(Reader([raw[:8]]), 0x1000))

    def test_load_probe_eyes_pass_the_native_eye_check_and_report_occlusion(self):
        import math
        from probe_vr_load import eye_rig, eye_command, summarize
        eyes = eye_rig((100., 50., -20.), (.6, 0., .8))
        (left, left_fov), (right, right_fov) = eyes
        for m in (left, right):
            right_axis, down, forward = m[0:3], m[4:7], m[8:11]
            # Native rows are right, down, forward: right x down must give forward.
            cross = (right_axis[1]*down[2]-right_axis[2]*down[1], right_axis[2]*down[0]-right_axis[0]*down[2],
                     right_axis[0]*down[1]-right_axis[1]*down[0])
            for a, b in zip(cross, forward):
                self.assertAlmostEqual(a, b)
            self.assertAlmostEqual(m[13], 50+1.6)
        self.assertAlmostEqual(math.dist(left[12:15], right[12:15]), .064)
        # native_eyes::valid: left < 0 < right, down < 0 < up, all within 1.5 rad; each eye wider outward.
        for fov in (left_fov, right_fov):
            self.assertTrue(fov[0] < -.01 < .01 < fov[1] and fov[2] < -.01 < .01 < fov[3])
            self.assertTrue(all(abs(f) < 1.5 for f in fov))
        self.assertGreater(-left_fov[0], left_fov[1])
        self.assertGreater(right_fov[1], -right_fov[0])
        packet = eye_command(11, eyes, (100., 50., -20.))
        self.assertEqual(len(packet), 208)
        self.assertEqual(struct.unpack_from('<4IQ2I', packet), (0x53455043, 2, 208, 1, 11, 250, 0))
        self.assertEqual(struct.unpack_from('<4f', packet, 96), tuple(struct.unpack('<4f', struct.pack('<4f', *left_fov))))
        self.assertEqual(struct.unpack_from('<3fI', packet, 192), (100, 50, -20, 1))
        # The eye sample's last word says whether the eye has its own occlusion object.
        raw = bytearray(payload())
        struct.pack_into('<I', raw, 64+144+44, 1)
        value = snapshot(Reader([bytes(raw), struct.pack('<Q', 2)]), 0)
        self.assertEqual([e['occlusion'] for e in value['eyes']], [0, 1])

        class Gpu:
            def window(self, start, end):
                return [(1.5, 80., 2800., 300., 9000.), (2.5, 90., 2800., 320., 9500.)]
        samples = [dict(t=t, frames=frames, frame_mb=mb, pairs=frames, threads={7: busy, 8: busy//4},
                        system=(idle, 16e7*t), read_bytes=t*2**21, commit_mb=commit)
                   for t, frames, mb, busy, idle, commit in ((0, 0, 30., 0, 0, 15000), (1, 100, 30., 0, 0, None),
                                                             (3, 300, 12., 2e7, 8e7, 17000))]
        threads = type('Threads', (), {'names': {7: 'render', 8: 'worker'}})()
        result = summarize('vr', samples, Gpu(), threads, 16, settle=1)
        # Measured from the first sample after settling: 200 frames in 2 s, the busiest thread a full core.
        self.assertEqual((result['seconds'], result['fps'], result['frame_ms']), (2, 100, 10))
        self.assertEqual(result['busiest_threads'][0], dict(thread=7, name='render', percent_of_core=100))
        self.assertEqual(result['gpu_utilization'], 85)
        self.assertEqual(result['render_mb_per_frame'], 21)
        self.assertEqual(result['pc_cpu_percent'], 75)
        self.assertEqual(result['disk_read_mb_per_s'], 2)
        # The most video memory in use and committed to the game in the phase, an unreadable commit skipped.
        self.assertEqual((result['gpu_memory_mb'], result['game_commit_mb']), (9500, 17000))

    def test_torn_read_retries(self):
        value = snapshot(Reader([payload(2), struct.pack('<Q', 4), payload(4), struct.pack('<Q', 4)]), 0)
        self.assertEqual(value['sequence'], 4)

    def test_writer_in_progress_is_not_published(self):
        self.assertIsNone(snapshot(Reader([payload(3), struct.pack('<Q', 3)]*8), 0))

    def test_short_memory_read_is_not_published(self):
        self.assertIsNone(snapshot(Reader([payload()[:-1], struct.pack('<Q', 2)]), 0))

    def test_old_dll_protocol_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'protocol mismatch'):
            snapshot(Reader([payload(version=2), struct.pack('<Q', 2)]), 0)


if __name__ == '__main__':
    unittest.main()

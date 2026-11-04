#pragma once
// First-person cutscenes (launcher: FIRST-PERSON CUTSCENES): where the head of
// the character a scene plays is, so the eyes can be inside it.
//
// While a scene plays the game's camera is an animated one, and the player
// character is an ordinary animated skeleton in it: usually the hero's own
// entity, but in some scenes (Peter waking up, the suit-up) a stand-in
// skeleton that plays him while the hero is parked out of sight. This module
// finds that skeleton's head joint, once per game frame, and hides what would
// be in front of the eyes there: the head, face, hair, eyes and mouth, and the
// suit's mask and lens pieces. The body and the hands stay visible.
//
// The eyes themselves are placed by the native views (stereo_probe.cpp,
// placeEyes): the XR worker sends them relative to this head (eye command
// anchored == 2, native_eyes::onHead) and the render thread resolves them with
// the head of the frame being rendered.
//
// Nothing here is a camera hook: nothing is written to the game but the
// hidden flag of entities this module hid itself (the game's own DrawOff and
// DrawOn handlers, as the hero's hiding does), and those go back when the scene
// is over.
#include "math.hpp"
#include <cstddef>
#include <cstdint>

namespace spidy::game_fp {
// What the module found, for the session report and the log.
struct Status {
    // 0 not wanted, 1 wanted but no head found yet, 2 hero's head, 3 stand-in's head, 4 stand-in's body twin,
    // 5 the last head held for a moment (its source just went away), 6 the scene's own camera (sceneCamera 2)
    uint32_t source{};
    uint32_t hidden{};  // entities this module keeps hidden right now
    uint32_t samples{}; // frames a head was sampled
    uint32_t misses{};  // frames wanted but no usable head
    uint32_t passes{};  // pool passes the worker made
    float passMs{};     // the last pass's length
    uint64_t vqCalls{}; // VirtualQuery calls made (each costs about 0.3 ms in this game: they are cached)
};
// Once, with the game's module base. The worker that looks for the scene's
// character starts with the first scene that wants first person (want), so
// a session without first-person cutscenes never has it.
bool start(uintptr_t exeBase);
// The XR worker, once per headset frame: a scene wants first person now.
// Going false puts back everything this module hid.
void want(bool on);
// The XR worker, once per headset frame, with want(): what the view is where no head is found. 0: nothing,
// the scene goes to the theatre screen. 1: the scene's own camera, where no head can be found (a head is
// looked for all the same, and takes over when it is found). 2: the scene's own camera always (launcher:
// DISABLE THEATER SCREEN ENTIRELY, without first-person cutscenes, or by the right stick's click with them):
// no character is looked for, nothing is hidden. The camera's place on a level frame is the "head" frame()
// and frameLevel() give then.
void sceneCamera(int mode);
// The last frame() gave the scene's own camera, not a head.
// The player's choice of whose eyes (X on the left controller, in a first-person scene): cycles every
// character drawn in the scene besides the hero's own entity that is known by its body asset (Miles, MJ,
// Spider-Man, and all of Peter's bodies), in pool order, and after the last the automatic choice.
// alternatives(): how many are there now (0: X is the game's); chosenCharacter(): 0 automatic, 1 one of them
// is chosen.
void cycleCharacter();
int alternatives();
int chosenCharacter();
bool onCamera();
// The render thread, once per game frame before the views are placed: the
// player's entity object this frame (0: none).
void hero(uintptr_t instance);
// The render thread, once per game frame: the game's own camera of the frame (the scene's, before the eyes
// are put on a head), so that the character in the shot can be told from one that is not. The tangents are
// half the width and half the height of its picture (0: not known; a wide cone then stands for it).
void camera(const Mat4& pose, float tangentHalfWidth = 0, float tangentHalfHeight = 0);
// The render thread, once per game frame, only while wanted: carries out the
// worker's hiding and samples the head. `head` is the head's pose in the world
// as rows right, down, forward and the position, orthonormal (the same layout
// as a native view's pose). False when no head can be placed this frame.
bool frame(Mat4& head);
// As frame(), for a scene whose head tracking is off: the head's place, but its axes are a level frame facing
// where the character faced when the scene (or the cut to its next shot) began, so only where he is follows
// him, not where he looks, and the viewer looks about freely from there.
bool frameLevel(Mat4& head);
// The heading the scene's view has in the world, for the play that follows it (the XR worker): the
// character's head (`level` false) or frameLevel's frame (`level` true), yaw in radians as the rig has it (0
// facing -z). False before one was sampled.
bool sceneYaw(bool level, float& yaw);
// The pose job's hook (native_body.cpp, writer), after the game wrote a character's model-space joints
// (`joints`, 64 bytes a joint, from the rig `rig`): while a first-person scene wants it, the head joint of
// the skeleton the view is on, and every joint under it, shrink to a point at the head joint, as SpidyVR's
// own body does for the player: what is skinned to the head, its face, hair and eyes and the suit's mask and
// lenses, is gone from inside the eyes, and the body stays. The head joint's place does not change.
// True when it shrank the joints.
bool shrinkHead(float* joints, uintptr_t rig);
// The body's rig reading (native_body.cpp, solveHero), whenever it has the hero's rig: the character's
// forward and up as vectors in the head joint's own frame, and the eyes' place from that joint along
// (forward, up, left), all read from the rest pose. The head joint's axes are the rig's own: without these a
// head is not placed.
void setHeadAxes(Vec3 forward, Vec3 up, Vec3 eyes);
// A head was sampled in the last 1.5 s (or, with sceneCamera, the camera was the view in it): the scene can
// be shown this way.
bool available();
Status status();
// The worker's latest line for the session log, when it is not the one `seen`
// already names (pass 0 at first): what it found, hid or lost.
bool note(uint32_t& seen, char* out, size_t size);
// Everything hidden by this module goes back at once (the render thread).
void release();
} // namespace spidy::game_fp

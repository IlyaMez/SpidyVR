#pragma once

// NEAR CLIP (vr_settings::Values::nearClipMm): how near the eyes the headset
// still shows things in immersive VR. The game's own camera, behind the hero,
// clips at 10 cm, which cut off hands held up before the face.
namespace spidy::near_clip {
// From the VR settings (game_xr.cpp), as they start and whenever they change.
void set(int millimetres);
// For the head's and the eyes' lenses (stereo_probe.cpp), metres.
float metres();
} // namespace spidy::near_clip

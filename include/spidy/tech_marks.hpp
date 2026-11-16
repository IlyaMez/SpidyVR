#pragma once
#include "body_calibration.hpp"
#include "gadget_wheel.hpp"
#include "web_visual.hpp"

// Spidy's marks in the tech style (UiStyle::tech), as the game's own HUD and
// menus draw theirs: lines a pixel or two wide in a pale blue white, cyan for
// what is picked or answers now, amber for what is equipped or finishes,
// red for danger; translucent blue panels edged with thin lines, the menus'
// corner brackets round what is lit; and lettering upright. Every shape has
// a soft dark halo under it, so it reads on the sky and on a lit wall, and
// the lit ones a faint glow of their colour. Unlike the printed marks, which
// hold poses, these ease in, fade and turn smoothly. Each draws the same
// thing in the same place as its printed namesake (web_visual.hpp,
// gadget_wheel.hpp, body_calibration.hpp), takes the same arguments, and
// draws nothing for the same bad ones; the public append functions call
// these for UiStyle::tech.
namespace spidy::tech {
// The style's colours, linear RGB. Line is the game's line white, text its
// menus' unpicked rows; cyan its picked row; panel its menus' blue.
inline constexpr Vec3 line{.79f, .89f, 1.f}, text{.58f, .68f, .80f}, cyan{.012f, .70f, 1.f},
    panel{.008f, .12f, .34f}, navy{.003f, .010f, .032f}, amber{1.f, .54f, .012f}, red{1.f, .045f, .03f},
    dim{.10f, .14f, .21f};
// Each hand's aim markers: cyan-blue for the left hand, amber-orange for the
// right, as the printed ones are blue and orange.
inline constexpr Vec3 hands[2] = {{.02f, .58f, 1.f}, {1.f, .42f, .03f}};

// The aim marker: anchor, four arcs round a diamond with ticks out at the
// gaps; air, a ring of faint dashes; blocked, an open red cross; target, three
// arcs with inward claws round a dashed ring turning the other way.
void appendAimMarker(std::vector<Vertex>& out, const AimMarker& marker, Vec3 viewer, float pixelAngle);
// Slow motion's meter: a watch face over the wrist, its focus in segments
// round an hourglass, a sweep turning round it in slow motion.
void appendSlowMotionMeter(std::vector<Vertex>& out, const SlowMotionView& view, Pose hand, Vec3 viewer,
                           float pixelAngle, float seconds);
// The pad layer's sign: a tag as the knockout's over the right wrist.
void appendPadLayerSign(std::vector<Vertex>& out, Pose hand, Vec3 viewer, float pixelAngle);
// A landed blow: a hit marker's four strokes springing out and a ring going
// out from it, amber for a hard blow, two rings for a finisher; the count
// on a callout.
void appendImpact(std::vector<Vertex>& out, const ImpactView& impact, Vec3 viewer, float pixelAngle);
// The mark over the enemy the next punch goes to: four arcs; locked, bolder
// with pointers in and a dashed ring turning; a call, chevrons running the
// way the fist must go over a ring of segments that runs down.
void appendCue(std::vector<Vertex>& out, const CueView& cue, Vec3 viewer, float pixelAngle);
// A knockout: a reticle closing on where he stood and a "KO" tag.
void appendKnockout(std::vector<Vertex>& out, const KnockoutView& knockout, Vec3 viewer, float pixelAngle);
// A blow a guard stopped: a hexagon going out from where the fist met him, a shield drawn in thin lines on a
// blue plate, and - when guard.txt gives him a counter to count towards - a callout up and to the right: a
// leader out to a tag, a panel with a bar at its left and a diamond in it for each blow he takes before he
// swings back (lit for those he has taken). The blow that sets off his counter has the chevron, the bar and
// the diamonds red. No lettering: the shield says it (the comic style has the word).
void appendBlock(std::vector<Vertex>& out, const BlockView& block, Vec3 viewer, float pixelAngle);
// Spider-Sense: five glowing wavy lines fanned up from his head, as the game
// draws its own round the hero's, in what the warning asks for, inside a ring
// of dashes closing in; amber with pointers in at its last notch.
void appendSense(std::vector<Vertex>& out, const SenseView& sense, Vec3 viewer, float pixelAngle);
void appendSenseSight(std::vector<Vertex>& out, const SenseSightView& view, Pose head, float pixelAngle,
                      const SenseSightConfig& config);
// The gadget wheel: a translucent blue ring of eight places with line
// pictures, the lit one's wedge cyan inside the menus' corner brackets, its
// name and charges in the middle.
void appendGadgetWheel(std::vector<Vertex>& out, const GadgetWheelView& view, Vec3 viewer, float pixelAngle);
// The calibration panel as one of the game's menu panels, and a ring of
// segments at each controller. Translucent: draw it after what has depth.
void appendCalibration(std::vector<Vertex>& out, const body_calibration::View& view, Vec3 viewer);
} // namespace spidy::tech

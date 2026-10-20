#include "spidy/lab_world.hpp"
#include "spidy/native_view.hpp"
#include "spidy/game_tracking.hpp"
#include "spidy/native_eye_frame.hpp"
#include "spidy/native_hud.hpp"
#include "spidy/native_eye_history.hpp"
#include "spidy/eye_job_table.hpp"
#include "spidy/eye_pair_state.hpp"
#include "spidy/native_rays.hpp"
#include "spidy/native_webs.hpp"
#include "spidy/native_render_memory.hpp"
#include "spidy/enclosure.hpp"
#include "spidy/game_pad.hpp"
#include "spidy/gadget_wheel.hpp"
#include "spidy/phone.hpp"
#include "spidy/controller_layout.hpp"
#include "spidy/game_swing.hpp"
#include "spidy/native_movement.hpp"
#include "spidy/presentation_gate.hpp"
#include "spidy/eye_resolution.hpp"
#include "spidy/monitor_rate.hpp"
#include "spidy/eye_snapshot.hpp"
#include "spidy/vr_settings.hpp"
#include "spidy/web_visual.hpp"
#include "spidy/web_pull.hpp"
#include "spidy/web_grab.hpp"
#include "spidy/lab_props.hpp"
#include "spidy/body_ik.hpp"
#include "spidy/crawl_stick.hpp"
#include "spidy/body_calibration.hpp"
#include "spidy/overlay_text.hpp"
#include "spidy/punch.hpp"
#include "spidy/shooter.hpp"
#include "spidy/slow_motion.hpp"
#include "spidy/hand_pose.hpp"
#include "spidy/impact_sound.hpp"
#include "spidy/web_strike.hpp"
#include "spidy/spider_sense.hpp"
#include "spidy/prompt_buttons.hpp"
#include "spidy/xr_layers.hpp"
#include "spidy/offer_pointing.hpp"
#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include "spidy/game_targets.hpp"
// Stand-ins for the game's classes, by the names the target scan's rules look
// for: MSVC lays out their type information as it did the game's.
class Component {
  public:
    virtual ~Component() = default;
};
class BotMoverManager : public Component {};
class HoverMoverManager : public BotMoverManager {};
class DocOckMoverManager : public HoverMoverManager {};
class BotMoverManagerGame : public BotMoverManager {};
class ThugBot : public Component {};
class SilverSable : public ThugBot {};
class CivilianBot : public Component {};
class BirdBot : public Component {};
class ThrowableHelper : public Component {};
class StatusEffectTrackerWebbed : public Component {};
class Unrelated : public Component {};
// A bot's states: the ones that have him down, by their own classes and by
// the ones the game derives from them, and one that has him fighting.
class BotStateBase : public Component {};
class BotStateAim : public BotStateBase {};
class BotStateDeathAnim : public BotStateBase {};
class BotStateDeathAnimGame : public BotStateDeathAnim {};
class BotStateRagdoll : public BotStateBase {};
class BotStateRagdollGame : public BotStateRagdoll {};
class BotStateWallStuck : public BotStateBase {};
// The states in which something else moves a bot, as the game derives them.
class BotStateAttachToActor : public Component {};
class BotStateNull : public BotStateBase {};
class BotStatePlayCinematic : public BotStateNull {};
class BotStatePlayCinematicGame : public BotStatePlayCinematic {};
class BotStatePlaySyncedAnimGuest : public BotStateBase {};
class BotStatePlaySyncedAnimGuestGame : public BotStatePlaySyncedAnimGuest {};
class BotStatePlaySyncedAnimHost : public BotStateBase {};
class BotStateCarried : public BotStateBase {};
class BotStateSlottedInteract : public Component {};
class BotStateFrozen : public Component {};
class BotStateRequestTakedownYank : public BotStateBase {};
class BotStateFlung : public BotStateBase {};
class EnemyTurret : public Component {};
extern "C" char __ImageBase;
#endif
using namespace spidy;
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void near(float a, float b, float epsilon = .001f) {
    if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a - b) > epsilon)
        throw std::runtime_error("Expected " + std::to_string(b) + ", got " + std::to_string(a));
}
struct TestWorld : World {
    Vec3 anchor{0, 20, 0};
    bool enabled = true, moving = false;
    std::optional<RayHit> raycast(Vec3 o, Vec3 d, float distance) const override {
        const float t = dot(anchor - o, d);
        if (enabled && t > 0 && t <= distance && length(o + d * t - anchor) < .02f)
            return RayHit{anchor, {}, 1, !moving};
        return {};
    }
    MoveResult sweep(Vec3, Vec3 to, float) const override {
        return {to, {}, false};
    }
    bool exists(std::uint64_t id) const override {
        return enabled && id == 1;
    }
};
Input aimed() {
    Input in;
    in.hands[0] = {{{0, 0, 0}, {.70710678f, 0, 0, .70710678f}}, {0, 0, 0}, true, 1, 1};
    return in;
}
// The point anchor over a street at y = 0, which a ray down meets.
struct Street : TestWorld {
    std::optional<RayHit> raycast(Vec3 o, Vec3 d, float distance) const override {
        if (const auto hit = TestWorld::raycast(o, d, distance))
            return hit;
        if (d.y < -1e-4f && o.y > 0 && -o.y / d.y <= distance)
            return RayHit{o + d * (-o.y / d.y), {0, 1, 0}, 9, true};
        return {};
    }
};
// The left hand at `from`, its grip held, aiming at `at`.
Input aimedAt(Vec3 from, Vec3 at) {
    Input in;
    in.hands[0] = {{from, arc({0, 0, -1}, normalized(at - from))}, {0, 0, 0}, true, 0, 1};
    return in;
}
// Two point anchors up to the left and right of a body on the y axis.
struct TwoAnchors : TestWorld {
    Vec3 anchors[2]{{-10, 20, 0}, {10, 20, 0}};
    std::optional<RayHit> raycast(Vec3 o, Vec3 d, float distance) const override {
        for (unsigned i = 0; i < 2; ++i) {
            const float t = dot(anchors[i] - o, d);
            if (t > 0 && t <= distance && length(o + d * t - anchors[i]) < .02f)
                return RayHit{anchors[i], {}, i + 1, true};
        }
        return {};
    }
    bool exists(std::uint64_t id) const override {
        return id == 1 || id == 2;
    }
    // Both grips squeezed, each hand aiming at its anchor from `from`.
    Input aimed(Vec3 from) const {
        Input in;
        for (unsigned i = 0; i < 2; ++i) {
            const float angle = std::atan2(from.x - anchors[i].x, anchors[i].y - from.y);
            in.hands[i] = {{from, Quat{0, 0, std::sin(angle / 2), std::cos(angle / 2)} *
                                      Quat{.70710678f, 0, 0, .70710678f}},
                           {0, 0, 0}, true, 0, 1};
        }
        return in;
    }
};
// A building for the wall tests: a box whose faces are walls a ray meets from
// outside, and whose top is a roof.
struct Building : TestWorld {
    Vec3 low{5, 0, -20}, high{15, 30, 20};
    std::optional<RayHit> raycast(Vec3 o, Vec3 d, float distance) const override {
        const float from[3]{o.x, o.y, o.z}, way[3]{d.x, d.y, d.z}, lo[3]{low.x, low.y, low.z},
            hi[3]{high.x, high.y, high.z};
        float enter = 0, leave = distance, normal[3]{};
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(way[axis]) < 1e-8f) {
                if (from[axis] < lo[axis] || from[axis] > hi[axis])
                    return {};
                continue;
            }
            float first = (lo[axis] - from[axis]) / way[axis], second = (hi[axis] - from[axis]) / way[axis];
            float out = -1;
            if (first > second) {
                std::swap(first, second);
                out = 1;
            }
            if (first > enter) {
                enter = first;
                normal[0] = normal[1] = normal[2] = 0;
                normal[axis] = out;
            }
            leave = std::min(leave, second);
            if (enter > leave)
                return {};
        }
        if (enter <= 0)
            return {};
        return RayHit{o + d * enter, {normal[0], normal[1], normal[2]}, 7, true};
    }
    bool exists(std::uint64_t id) const override {
        return id == 7;
    }
};
SwingConfig walled() {
    SwingConfig c;
    c.walls = true;
    return c;
}
// The swing's walls with the ground mount on, as they were from October 10 to
// 14: what the mount does when it is asked to.
SwingConfig mounted() {
    SwingConfig c = walled();
    c.mountFromGround = true;
    return c;
}
// Flies a body through native steps: the game moves it where each prediction
// asks, as a mover that meets nothing does.
Body fly(Swing& s, const WorldQueries& w, Body body, const Input& in, float seconds, float dt = 1.f / 60) {
    for (float t = 0; t < seconds - dt / 2; t += dt) {
        const auto intent = s.predictNativeStep(dt, in, w, body);
        check(intent.valid, "prediction rejected");
        body = {intent.target, intent.velocity, false};
    }
    return body;
}
// Grab targets on a straight-line integrator: a commanded target moves as
// the command's law gives it, under gravity; the others keep their velocity.
struct GrabTargets : TargetQueries {
    std::vector<GrabTarget> items;
    void add(std::uint64_t id,Vec3 position,float mass=25,float radius=.4f,TargetKind kind=TargetKind::Object) {
        items.push_back({id,kind,position,{},mass,radius});
    }
    std::optional<GrabTarget> pick(Vec3 o,Vec3 d,float distance,float cone) const override {
        std::optional<GrabTarget> best;float bestMiss=cone;
        for(const auto& t:items)
            if(const float miss=rayMiss(o,d,distance,t.position,t.radius);miss<=bestMiss){bestMiss=miss;best=t;}
        return best;
    }
    std::optional<GrabTarget> find(std::uint64_t id) const override {
        for(const auto& t:items)if(t.id==id)return t;
        return {};
    }
    void characters(std::vector<GrabTarget>& out) const override {
        for(const auto& t:items)if(t.kind==TargetKind::Character)out.push_back(t);
    }
    std::vector<std::pair<std::uint64_t,std::uint64_t>> owners; // surface, target
    std::optional<std::uint64_t> owner(std::uint64_t surface) const override {
        for(const auto& [s,t]:owners)if(s==surface)return t;
        return {};
    }
    // With ground: every target falls and rests on a floor at y = 0, sliding with friction.
    bool ground{};
    void apply(float dt,const std::vector<TargetCommand>& commands) {
        for(auto& t:items) {
            bool commanded=false;
            for(const auto& c:commands)if(c.id==t.id){t.velocity=advance(c,t.position,t.velocity,dt,{0,-9.81f,0});commanded=true;}
            if(ground&&!commanded)t.velocity.y-=9.81f*dt;
            t.position+=t.velocity*dt;
            if(ground&&t.position.y<t.radius) {
                t.position.y=t.radius;t.velocity.y=std::max(t.velocity.y,0.f);
                const Vec3 flat{t.velocity.x,0,t.velocity.z};const float s=length(flat),slow=.6f*9.81f*dt;
                const Vec3 left=s>slow?flat*((s-slow)/s):Vec3{};t.velocity.x=left.x;t.velocity.z=left.z;
            }
        }
    }
};
// The left hand at (0,1,0), aiming along -Z, grip squeezed.
Input forward() {
    Input in;
    in.hands[0]={{{0,1,0},{}},{0,0,0},true,0,1};
    return in;
}
unsigned events(const WebGrab& grab,GrabEventKind kind) {
    unsigned n=0;
    for(const auto& e:grab.events())n+=e.kind==kind;
    return n;
}
// One input sample, then physics at `hz` until the next one.
void settle(WebGrab& grab,GrabTargets& targets,const World& world,const Body& player,const Input& in,
            float seconds,int hz=360) {
    grab.claim(seconds,in,world,targets,player);
    const int steps=std::max(1,static_cast<int>(std::lround(seconds*hz)));
    std::vector<TargetCommand> out;
    for(int s=0;s<steps;++s){out.clear();grab.step(seconds/steps,world,targets,out);targets.apply(seconds/steps,out);}
}
SwingConfig inert() {
    SwingConfig c;
    c.gravity = 0;
    c.airAcceleration = 0;
    return c;
}
// A small humanoid in the game's joint layout (four rows: x, y, z axes, then
// the position), facing +z with its left on +x, its rest pose a T-pose. With
// oddFrames every joint frame is turned and the left side mirrored, as game
// rigs do, so the solver cannot lean on joint axes. With heroHand its hands
// are Spider-Man's own (four fingers and heroThumbs).
struct TestBody {
    std::vector<float> rest;
    body::Rig rig;
    Vec3 at(const std::vector<float>& pose,int joint) const {return {pose[joint*16+12],pose[joint*16+13],pose[joint*16+14]};}
};
TestBody testBody(bool oddFrames=false,bool heroHand=false) {
    struct J {int parent;Vec3 p;};
    std::vector<J> joints={
        {-1,{0,0,0}},{0,{0,1.f,0}},{1,{0,1.12f,0}},{2,{0,1.25f,0}},{3,{0,1.38f,0}},{4,{0,1.52f,0}},{5,{0,1.62f,0}},
        {6,{.032f,1.7f,.08f}},{6,{-.032f,1.7f,.08f}},
        {4,{.03f,1.47f,.02f}},{9,{.18f,1.46f,0}},{10,{.44f,1.46f,0}},{11,{.74f,1.46f,0}},{12,{.83f,1.46f,0}},
        {12,{.77f,1.45f,.04f}},
        {4,{-.03f,1.47f,.02f}},{15,{-.18f,1.46f,0}},{16,{-.44f,1.46f,0}},{17,{-.74f,1.46f,0}},{18,{-.83f,1.46f,0}},
        {18,{-.77f,1.45f,.04f}},
        {1,{.1f,.95f,0}},{21,{.1f,.5f,.02f}},{22,{.1f,.08f,0}},{23,{.1f,0,.13f}},
        {1,{-.1f,.95f,0}},{25,{-.1f,.5f,.02f}},{26,{-.1f,.08f,0}},{27,{-.1f,0,.13f}},
        // A left middle finger, palm down: base in the palm, knuckle, middle joint, tip, end.
        {12,{.77f,1.46f,0}},{29,{.83f,1.46f,0}},{30,{.875f,1.46f,0}},{31,{.905f,1.46f,0}},{32,{.93f,1.46f,0}},
        // Its thumb, ahead of the palm: base, two joints, end.
        {12,{.76f,1.45f,.03f}},{34,{.78f,1.45f,.06f}},{35,{.8f,1.45f,.085f}},{36,{.82f,1.45f,.1f}},
    };
    if(heroHand){
        // Spider-Man's left hand at rest (the game's rig), turned so its fingers run along +x and the palm a rest
        // pose is assumed to hold (down, across the fingers) is -y. Its own palm faces 30 degrees from there,
        // toward the thumb. Its middle finger and thumb take the places of the small ones (and the hand's
        // finger and thumb joints theirs); its index, ring and little fingers follow, then the right hand, the
        // left's mirror image.
        const Vec3 fingers[4][5]={
            {{.7547f,1.4682f,.0254f},{.8261f,1.4679f,.0223f},{.8739f,1.4709f,.0272f},{.9036f,1.4727f,.0302f},{.931f,1.4743f,.0329f}},
            {{.7521f,1.4625f,.008f},{.8226f,1.46f,0},{.8791f,1.4514f,-.0046f},{.9141f,1.4459f,-.0075f},{.9464f,1.441f,-.0102f}},
            {{.7507f,1.4533f,-.0112f},{.8093f,1.4475f,-.0189f},{.8603f,1.4276f,-.0306f},{.8931f,1.4147f,-.0382f},{.9241f,1.4026f,-.0453f}},
            {{.7469f,1.4398f,-.021f},{.7989f,1.4339f,-.0358f},{.8333f,1.4134f,-.0501f},{.8545f,1.4007f,-.0589f},{.8741f,1.3891f,-.0671f}}};
        const Vec3 thumb[4]={{.7509f,1.4536f,.0366f},{.7913f,1.4578f,.0674f},{.8164f,1.4634f,.0953f},{.8359f,1.4701f,.1244f}};
        joints[13].p=fingers[1][1];joints[14].p=thumb[0];
        for(int k=0;k<5;++k)joints[29+k].p=fingers[1][k];
        for(int k=0;k<4;++k)joints[34+k].p=thumb[k];
        for(const int f:{0,2,3}){
            const int first=static_cast<int>(joints.size());
            for(int k=0;k<5;++k)joints.push_back({k?first+k-1:12,fingers[f][k]});
        }
        const auto mirror=[](Vec3 v){return Vec3{-v.x,v.y,v.z};};
        joints[19].p=mirror(fingers[1][1]);joints[20].p=mirror(thumb[0]);
        for(int f=0;f<4;++f){
            const int first=static_cast<int>(joints.size());
            for(int k=0;k<5;++k)joints.push_back({k?first+k-1:18,mirror(fingers[f][k])});
        }
        const int first=static_cast<int>(joints.size());
        for(int k=0;k<4;++k)joints.push_back({k?first+k-1:18,mirror(thumb[k])});
    }
    TestBody b;
    const int n=static_cast<int>(joints.size());
    b.rest.assign(n*16,0.f);
    for(int j=0;j<n;++j){
        Quat q{};
        if(oddFrames)q=body::unit({std::sin(j*1.3f),std::cos(j*.7f),std::sin(j*.31f+1),1.5f});
        Vec3 axes[3]={q.rotate({1,0,0}),q.rotate({0,1,0}),q.rotate({0,0,1})};
        if(oddFrames&&joints[j].p.x>.01f)axes[0]=axes[0]*-1.f;
        float* m=&b.rest[j*16];
        for(int r=0;r<3;++r){m[r*4]=axes[r].x;m[r*4+1]=axes[r].y;m[r*4+2]=axes[r].z;}
        m[12]=joints[j].p.x;m[13]=joints[j].p.y;m[14]=joints[j].p.z;m[15]=1;
        b.rig.parent.push_back(static_cast<int16_t>(joints[j].parent));
    }
    auto& r=b.rig;
    r.pelvis=1;r.spine={2,3,4};r.neck={5};r.head=6;r.eyes[0]=7;r.eyes[1]=8;
    r.arms[0]={9,10,11,12,13,14};r.arms[1]={15,16,17,18,19,20};
    r.arms[0].fingers={{29,30,31,32,33}};
    r.arms[0].thumbChain={34,35,36,37};
    if(heroHand){
        r.arms[0].fingers={{38,39,40,41,42},{29,30,31,32,33},{43,44,45,46,47},{48,49,50,51,52}};
        r.arms[1].fingers={{53,54,55,56,57},{58,59,60,61,62},{63,64,65,66,67},{68,69,70,71,72}};
        r.arms[1].thumbChain={73,74,75,76};
    }
    r.legs[0]={21,22,23};r.legs[1]={25,26,27};
    check(body::prepare(r,b.rest),"the test rig was rejected");
    return b;
}
// testBody's heroHand: each hand's thumb (0 the left, 1 the right).
constexpr int heroThumbs[2][4]={{34,35,36,37},{73,74,75,76}};
// Where Spider-Man's palm faces in `pose`: across its knuckles and along its
// bones in the palm, square to the hand's fingers.
Vec3 heroPalm(const TestBody& b,const std::vector<float>& pose,int side){
    const auto& arm=b.rig.arms[side];
    Vec3 bones{};
    for(const auto& f:arm.fingers)bones+=normalized(b.at(pose,f[1])-b.at(pose,f[0]));
    const Vec3 along=normalized(b.at(pose,arm.finger)-b.at(pose,arm.hand));
    // From the index finger's knuckle to the little one's, a left hand's palm is on the right.
    Vec3 n=cross(normalized(b.at(pose,arm.fingers.back()[1])-b.at(pose,arm.fingers.front()[1])),normalized(bones));
    if(side)n=n*-1.f;
    return normalized(n-along*dot(n,along));
}
// A finger's hinge in `pose`: across its bone in the palm and the palm, a
// positive turn closing it.
Vec3 heroHinge(const TestBody& b,const std::vector<float>& pose,const std::vector<int16_t>& finger,int side){
    return normalized(cross(normalized(b.at(pose,finger[1])-b.at(pose,finger[0])),heroPalm(b,pose,side)));
}
// How far `chain[k]` bends from the bone before it about `hinge`, radians.
float bendOf(const TestBody& b,const std::vector<float>& pose,const std::vector<int16_t>& chain,size_t k,Vec3 hinge){
    const auto flat=[&](Vec3 v){v=normalized(v);return v-hinge*dot(v,hinge);};
    const Vec3 x=flat(b.at(pose,chain[k])-b.at(pose,chain[k-1])),y=flat(b.at(pose,chain[k+1])-b.at(pose,chain[k]));
    return std::atan2(dot(cross(x,y),hinge),dot(x,y));
}
// A headset looking along model +z (OpenXR looks along -z), eyes at `eyes`.
body::Targets lookingAhead(Vec3 eyes){
    body::Targets t;t.head=true;t.eyes=eyes;t.facing=Quat::yaw(3.14159265f);
    return t;
}
// A controller held thumb up with the knuckles along model `knuckles`, by a
// player facing +z (whose right is model -x): its grip pose.
Quat gripFacing(Vec3 knuckles){
    const Vec3 y=normalized(knuckles)*-1.f,x{-1,0,0};
    return body::fromAxes(x,y,cross(x,y));
}
// The wrist the solver aims for from a grip pose (Config::wristFromGrip).
Vec3 wristOf(const body::Targets::Hand& h,int side,const body::Config& c={}){
    return h.grip+h.orientation.rotate({side==0?-c.wristFromGrip.x:c.wristFromGrip.x,c.wristFromGrip.y,c.wristFromGrip.z});
}
// Solves a fresh copy of the game's pose each frame, as the game hands one
// over, until the body has fully taken over.
std::vector<float> solved(const TestBody& b,const body::Targets& t,body::State& s,const body::Config& c={},
                          float scale=1,body::Result* result=nullptr,const std::vector<float>* from=nullptr,
                          float armScale=1){
    std::vector<float> pose;
    body::Result r;
    for(int i=0;i<4;++i){
        pose=from?*from:b.rest;
        body::Pose view(pose.data(),static_cast<int>(b.rig.parent.size()));
        r=body::solve(view,b.rig,t,c,s,true,.1f,scale,armScale);
    }
    if(result)*result=r;
    return pose;
}
XrFrame trackedFrame() {
    XrFrame f;f.focused=f.valid=true;f.predictedDisplayTime=1;
    f.head.position={0,1.7f,0};
    for(unsigned i=0;i<2;++i) {
        f.eyes[i]={{{i==0?-.032f:.032f,1.7f,0},{}},{-.8f,.7f,-.75f,.8f},1000,1000};
        auto& h=f.hands[i];h.valid=true;
        h.aim=h.grip={{i==0?-.3f:.3f,1.2f,-.5f},{}};h.trigger=h.squeeze=1;
    }
    return f;
}
// Plays `seconds` of 90 Hz frames with the player's actor up `up`, its feet at `feet`.
GameMotionFrame playFor(GameTrackingRig& rig, XrFrame& f, Vec3 feet, Vec3 up, float seconds) {
    GameMotionFrame out;
    for (int i = 0; i < static_cast<int>(seconds * 90 + .5f); ++i) {
        ++f.predictedDisplayTime;
        out = rig.update(f, feet, {0, 0, -1}, true, up);
    }
    return out;
}
// Plays `seconds` of 90 Hz frames with the player held on a surface (or, with
// no normal, on none), its actor's up `up`.
GameMotionFrame holdFor(GameTrackingRig& rig, XrFrame& f, Vec3 feet, const SurfaceHold& hold, float seconds,
                        Vec3 up = {0, 1, 0}) {
    GameMotionFrame out;
    for (int i = 0; i < static_cast<int>(seconds * 90 + .5f); ++i) {
        ++f.predictedDisplayTime;
        out = rig.update(f, feet, {0, 0, -1}, true, up, true, hold);
    }
    return out;
}
// One 90 Hz frame for a flip: A held or not, the player in the air or not.
GameMotionFrame flipFrame(GameTrackingRig& rig, XrFrame& f, bool jump, bool airborne = true) {
    ++f.predictedDisplayTime;
    f.jump = jump;
    return rig.update(f, {}, {0, 0, -1}, true, {0, 1, 0}, airborne);
}
bool levelTilt(Quat q) {
    return q.x == 0 && q.y == 0 && q.z == 0 && q.w == 1;
}
// A world matrix's row: 0 right, 1 down (worldPose), 2 ahead, 3 the place.
Vec3 matrixRow(const Mat4& m, int r) {
    return {m[r * 4], m[r * 4 + 1], m[r * 4 + 2]};
}
// A player in a T-pose in the tracking space (OpenXR axes: -z ahead, +x right, +y up from the floor): eyes
// `eyes` high, each wrist `arm` (the left) and `rightArm` (the right; 0: the same) from the avatar's shoulder
// at the player's size (Spider-Man's proportions), straight out to its side; both triggers at `trigger`.
body_calibration::Sample tPose(float eyes,float arm,float rightArm=0,float trigger=1){
    body_calibration::Sample s;
    s.headTracked=true;
    s.head={{0,eyes,0},{}};
    const body_calibration::Proportions p;
    const float scale=body_calibration::bodyScale(eyes,p);
    const Vec3 forward{0,0,-1},up{0,1,0},left{-1,0,0};
    for(int i=0;i<2;++i){
        const Vec3 sh=p.shoulders[i];
        const Vec3 shoulder=s.head.position+(forward*sh.x+up*sh.y+left*sh.z)*scale;
        const Vec3 side=i==0?left:left*-1.f;
        const Vec3 wrist=shoulder+side*(i==1&&rightArm>0?rightArm:arm);
        // Knuckles (the grip's -y) out along the arm, thumb up (-z).
        const Vec3 y=side*-1.f,z{0,-1,0};
        const Quat grip=body::fromAxes(cross(y,z),y,z);
        s.grips[i]={wrist-grip.rotate({i==0?-.02f:.02f,.09f,0}),grip};
        s.handTracked[i]=true;
        s.triggers[i]=trigger;
    }
    s.seconds=1.f/90;
    return s;
}
// Feeds `sample` for `frames` frames; true when the calibration finished on one of them.
bool hold(body_calibration::Calibration& c,const body_calibration::Sample& sample,int frames){
    bool finished=false;
    for(int i=0;i<frames;++i)finished=c.update(sample)||finished;
    return finished;
}
int main() {
    int total = 0, failed = 0;
    auto test = [&](const char* name, const std::function<void()>& f) {
        ++total;
        try {
            f();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
        }
    };
    test("native view conversion preserves identity and meter scale", [] {
        const Mat4 original{1,0,0,0, 0,-1,0,0, 0,0,-1,0, 10,20,30,1};
        const auto same=native_view::relativePose(original,{});
        for(int i=0;i<16;++i)near(same[i],original[i]);
        const auto moved=native_view::relativePose(original,{{.25f,.5f,-1.f},{}});
        near(moved[12],10.25f);near(moved[13],20.5f);near(moved[14],29.f);
        check(native_view::validPose(moved),"invalid orthonormal output");
    });
    test("native view yaw rotates around head without moving it", [] {
        const Mat4 original{1,0,0,0, 0,-1,0,0, 0,0,-1,0, 10,20,30,1};
        const auto turned=native_view::relativePose(original,{{},Quat::yaw(1.5707963268f)});
        near(turned[8],-1);near(turned[9],0);near(turned[10],0);
        for(int i=12;i<16;++i)near(turned[i],original[i]);
        check(native_view::validPose(turned),"yaw broke handedness");
    });
    test("native eye overlay uses the same world camera as the scene", [] {
        const Mat4 basis{1,0,0,0,0,-1,0,0,0,0,-1,0,0,0,0,1};
        const Pose eye{{10,20,30},Quat::yaw(.7f)};
        const auto native=native_view::relativePose(basis,eye);
        const auto actual=native_view::view(native), expected=viewMatrix(eye);
        for(unsigned i=0;i<16;++i) near(actual[i],expected[i]);
    });
    test("native eye poses retain IPD after rotated world mapping", [] {
        const Mat4 original{0,0,-1,0, 0,-1,0,0, -1,0,0,0, 10,20,30,1};
        const auto left=native_view::relativePose(original,{{-.032f,0,0},{}});
        const auto right=native_view::relativePose(original,{{.032f,0,0},{}});
        near(left[14]-right[14],.064f);
        near(left[12],right[12]);near(left[13],right[13]);
        check(native_view::validPose(left)&&native_view::validPose(right),"invalid eye matrices");
    });
    test("native view rejects nonfinite skewed and mirrored matrices", [] {
        Mat4 m{1,0,0,0, 0,-1,0,0, 0,0,-1,0, 0,0,0,1};
        m[4]=.2f;check(!native_view::validPose(m),"accepted shear");
        m[4]=0;m[0]=-1;check(!native_view::validPose(m),"accepted reflection");
        m[0]=1;m[12]=std::numeric_limits<float>::quiet_NaN();
        check(!native_view::validPose(m),"accepted NaN");
    });
    test("native distance scale is the engine's for the game camera and a headset eye", [] {
        // Read from the game on October 19: its camera (65 x 39 degrees, 16:9) and a
        // Quest 3 eye's asymmetric lens (98 x 96 degrees).
        native_view::Descriptor game{},eye{};
        game.values[0x410/4]=1.1345f;game.values[0x414/4]=.6882f;game.values[0x41c/4]=16.f/9;
        eye.values[0x410/4]=1.7104f;eye.values[0x414/4]=1.6755f;eye.values[0x41c/4]=1.0544f;
        near(native_view::distanceScale(game),1.0588f);
        near(native_view::distanceScale(eye),.5863f);
        // Wider than 16:9 it is judged across the height: the same height, the same scale.
        auto wide=game;wide.values[0x41c/4]=2.37f;wide.values[0x410/4]=1.4f;
        near(native_view::distanceScale(wide),1.0588f);
        // Flag 2 at +438: the bare numerator.
        auto bare=eye;const uint32_t flags=2;std::memcpy(&bare.values[0x438/4],&flags,4);
        near(native_view::distanceScale(bare),.6745f);
    });
    test("eye detail gives a wide eye the engine's 68-degree distances and never less than its own", [] {
        native_view::Descriptor eye{};
        eye.values[0x410/4]=1.7104f;eye.values[0x41c/4]=1.0544f;
        near(native_view::eyeDetail(eye),1.7056f);
        near(native_view::distanceScale(eye)*native_view::eyeDetail(eye),1);
        auto narrow=eye;narrow.values[0x410/4]=1.0472f; // 60 degrees: already further than 68 gives
        near(native_view::eyeDetail(narrow),1);
        auto widest=eye;widest.values[0x410/4]=2.6f; // 149 degrees: held at three times its own
        near(native_view::eyeDetail(widest),3);
        auto broken=eye;broken.values[0x410/4]=std::numeric_limits<float>::quiet_NaN();
        near(native_view::eyeDetail(broken),1);
        broken.values[0x410/4]=0;
        near(native_view::eyeDetail(broken),1);
    });
    test("texture requests are sized as the engine sizes them for an image behind a lens", [] {
        // An eye's 1352 x 1504 scene (67%) of its 2016 x 2240 image behind half tangents
        // 1.171 and 1.1106, as read from the game.
        near(native_view::requestScale(1352,1504,1.171f,1.1106f),677.1f,.1f);
        near(native_view::requestScale(2016,2240,1.171f,1.1106f)/native_view::requestScale(1352,1504,1.171f,1.1106f),
             2240.f/1504);
        near(native_view::requestScale(2000,1000,1,1),1000); // the side with more pixels per tangent
    });
    test("game rig uses one tracking sample for head hands and both eyes", [] {
        GameTrackingRig rig;auto f=trackedFrame();
        auto out=rig.update(f,{10,20,30},{0,0,-1},true);
        check(out.active&&out.releaseWebs,"initial activation must reset web tracking");
        near(out.head[12],10);near(out.head[13],21.7f);near(out.head[14],30);
        near(out.eyes[1][12]-out.eyes[0][12],.064f);
        near(out.hands[0].position.x,9.7f);near(out.hands[1].position.x,10.3f);
        check(out.predictedDisplayTime==f.predictedDisplayTime,"lost prediction timestamp");
        near(out.fovs[0].left,-.8f);
        f.predictedDisplayTime=2;out=rig.update(f,{10,20,30},{0,0,-1},true);
        check(out.swing.hands[0].tracked&&out.swing.hands[1].tracked,"hands not independent");
        near(out.swing.hands[0].gripRelativeToHead.y,-.5f);
    });
    test("each hand aims from its web shooter, on the wrist the body draws, where the hand points", [] {
        // Meta's runtimes, and Virtual Desktop's, turn the aim pose 30 degrees
        // from the hand toward the thumb; the hand aims along its own grip.
        GameTrackingRig rig; auto f=trackedFrame();
        const Quat aimTurn=Quat::around({1,0,0},-1.0471976f);
        f.hands[0].grip.orientation=Quat::around({0,1,0},.7f)*Quat::around({0,0,1},1.2f);
        f.hands[1].grip.orientation=Quat::around({1,0,0},.4f);
        for(auto& h:f.hands)h.aim.orientation=h.grip.orientation*aimTurn;
        rig.update(f,{10,20,30},{1,0,0},true);
        f.predictedDisplayTime=2;
        const auto out=rig.update(f,{10,20,30},{1,0,0},true);
        const body::Config c;
        for(unsigned i=0;i<2;++i){
            const auto& grip=out.grips[i];const auto& aim=out.swing.hands[i].aim;
            // From the wrist out past the knuckles, the thumb's side up.
            near(length(aim.orientation.rotate({0,0,-1})-grip.orientation.rotate({0,-1,0})),0);
            near(length(aim.orientation.rotate({0,1,0})-grip.orientation.rotate({0,0,-1})),0);
            // On the palm's side of the wrist the body puts there.
            const Vec3 wrist=grip.position+grip.orientation.rotate(
                {i==0?-c.wristFromGrip.x:c.wristFromGrip.x,c.wristFromGrip.y,c.wristFromGrip.z});
            const Vec3 palm=grip.orientation.rotate({i==0?1.f:-1.f,0,0});
            check(length(aim.position-wrist)<.035f,"the web shooter is not on the wrist");
            check(dot(aim.position-wrist,palm)>.02f,"the web shooter is not on the palm's side of the wrist");
            check(length(aim.position-webShooter(grip,i).position)<1e-4f,"the webs leave from elsewhere");
        }
        // The controller's own aim pose steers nothing.
        f.predictedDisplayTime=3;f.hands[0].aim.orientation=Quat::yaw(.4f);
        const auto again=rig.update(f,{10,20,30},{1,0,0},true);
        near(length(again.swing.hands[0].aim.orientation.rotate({0,0,-1})-
                    out.swing.hands[0].aim.orientation.rotate({0,0,-1})),0);
    });
    test("native grounded prediction never requests gravity through support", [] {
        TestWorld world;world.enabled=false;Swing swing;
        Body actual{{10,20,30},{0,-.2f,0},true}; Input in;
        for(int i=0;i<120;++i) {
            const auto intent=swing.predictNativeStep(.02f,in,world,actual);
            check(intent.valid,"ground sample rejected");near(intent.target.y,20);near(intent.velocity.y,0);
        }
        in.jump=true;
        const auto jump=swing.predictNativeStep(.02f,in,world,actual);
        check(jump.target.y>20&&jump.velocity.y>0,"support prevented intentional takeoff");
    });
    test("native support classification distinguishes floor slide and flight", [] {
        check(native_movement::groundedContact(0),"flat support reported airborne");
        check(!native_movement::groundedContact(1),"sliding reported flat support");
        check(!native_movement::groundedContact(2),"flight reported grounded");
        check(!native_movement::groundedContact(255),"unknown contact reported grounded");
        check(native_movement::collisionEnabled(0x2060000),"walking cannot sweep");
        check(native_movement::collisionEnabled(0x2060010),"airborne cannot sweep");
        check(!native_movement::collisionEnabled(0x2060001),"perch bypass accepted");
    });
    test("a movement command reaches the next step however late that step comes", [] {
        using native_movement::commandApplies;
        check(commandApplies(100,150,7,7),"a command was refused within its lease");
        check(commandApplies(149,150,9,7),"a lease ended early");
        check(!commandApplies(150,150,9,7),"a lease outlasted itself");
        // A paused game, or a long frame: the first step after the command
        // still takes it; one after that within no lease does not.
        check(commandApplies(60000,150,7,7),"the step after a pause ran without its command");
        check(!commandApplies(60000,150,8,7),"an expired command governed a second step");
    });
    test("a movement command moves a step of any length, 2 m at most, and is never refused for it", [] {
        using namespace native_movement;
        // The steps that dropped the player in the October 10 headset report,
        // the longest of that session, and a hitch.
        for (float dt : {.0504f, .0509f, .0517f, .0665f, .3f})
            check(drivable(dt), "a long step ran without its command");
        using limits = std::numeric_limits<float>;
        check(!drivable(0) && !drivable(-.01f), "a step of no length was driven");
        check(!drivable(limits::quiet_NaN()) && !drivable(limits::infinity()), "a step of no known length was driven");
        const Vec3 swinging{-10.3f, 9.7f, -28.7f};
        near(length(stepDelta(swinging, .0509f) - swinging * .0509f), 0);
        // Faster than 2 m a step: that far the command's way, not left to the game.
        const Vec3 far = stepDelta({0, 39, -52}, .05f);
        near(length(far), farthestStep);
        near(far.y / far.z, 39.f / -52);
        check(far.x == 0 && far.z < 0, "a shortened step turned");
        near(length(stepDelta(swinging, .3f)), farthestStep);
    });
    test("web takeoff waits for real clearance and retains a yank across landing", [] {
        SwingTakeoff t;
        auto r=t.update(1000,true,true,true,true,true,{0,1,0},{},{0,9,0});
        check(r.waiting&&!r.jump,"takeoff did not begin with a released jump");
        r=t.update(1080,true,true,true,false,false,{0,1,0},{},{});
        check(r.waiting&&r.jump,"native jump never pressed");
        // The native flag changes before the jump has lifted the actor. An
        // early velocity lease here would replace the game's launch velocity.
        r=t.update(1130,true,false,true,false,false,{0,1,0},{},{});
        check(r.waiting&&!r.resumed,"air flag alone resumed control");
        r=t.update(1160,true,false,true,false,false,{0,1.2f,0},{0,6,0},{});
        check(r.waiting,"one transient sample resumed control");
        r=t.update(1180,true,false,true,false,false,{0,1.35f,0},{0,6,0},{});
        check(r.resumed&&!r.waiting&&!r.jump,"takeoff never completed");
        near(r.launchVelocity.y,9);
        // Landing with the SAME held web can take off again.
        r=t.update(2000,true,true,true,true,false,{0,1,0},{},{0,8,0});
        check(r.waiting,"existing ownership prevented another takeoff");
        r=t.update(2080,true,true,true,true,false,{0,1,0},{},{});
        check(r.jump,"landed web cannot request native jump");
    });
    test("a takeoff from the game's crawl is off once it is clear of the wall, not above where it began", [] {
        // The wall x = 0 with its normal +x. A jump off it goes out and a little down (October 14, the first
        // mission's shaft: 70.27 m to 69.26 m), so "12 cm above where it began" held the web a third of a
        // second longer than it needed. Along the wall's normal it is off as soon as it is clear.
        SwingTakeoff t;const Vec3 n{1,0,0};
        auto r=t.update(1000,true,true,true,true,true,{.3f,70,0},{},{},false,n);
        check(r.waiting&&!r.jump,"takeoff did not begin with a released jump");
        r=t.update(1080,true,true,true,false,false,{.3f,70,0},{},{},false,n);
        check(r.waiting&&r.jump,"native jump never pressed");
        r=t.update(1130,true,false,true,false,false,{.5f,69.9f,0},{3,-1,0},{},false,n);
        check(r.waiting,"one sample off the wall resumed control");
        r=t.update(1160,true,false,true,false,false,{.7f,69.8f,0},{3,-1,0},{},false,n);
        check(r.resumed&&!r.waiting,"a body clear of its wall was still waited on");
        // The ground is unchanged: falling is not taking off.
        SwingTakeoff g;
        g.update(1000,true,true,true,true,true,{0,1,0},{},{});
        g.update(1080,true,true,true,false,false,{0,1,0},{},{});
        r=g.update(1130,true,false,true,false,false,{0,.8f,0},{},{});
        r=g.update(1160,true,false,true,false,false,{0,.6f,0},{},{});
        check(r.waiting&&!r.resumed,"a body falling from the ground was taken for one taking off");
    });
    test("takeoff retries a missed input edge then waits for a new gesture", [] {
        SwingTakeoff t;
        unsigned edges{};bool held{};
        for(uint64_t now=0;now<=1600;now+=20) {
            const auto r=t.update(now,true,true,true,true,false,{},{},{});
            edges+=r.jump&&!held;held=r.jump;
        }
        check(edges==2,"takeoff retried endlessly or did not retry");
        check(t.phase()==SwingTakeoff::TimedOut,"failed takeoff did not stop");
        auto r=t.update(1700,true,true,true,true,true,{},{},{});
        check(r.waiting,"new yank cannot retry after timeout");
        r=t.update(1800,false,true,true,false,false,{},{},{});
        check(!r.waiting&&!r.jump&&t.phase()==SwingTakeoff::Idle,"released web kept jumping");
        r=t.update(2000,true,true,true,false,false,{},{},{});
        check(!r.waiting&&!r.jump,"passive slack web started another jump");
    });
    test("native jump remains usable during owned flight and honors takeoff release", [] {
        constexpr uint32_t jump=1u<<4;
        check(swingNativeKeys(true,true,jump|1,SwingTakeoff::Idle,false)==jump,"ownership swallowed jump");
        check(swingNativeKeys(true,false,jump|1,SwingTakeoff::ReleaseJump,false)==1,"jump edge was not rearmed");
        check(swingNativeKeys(true,false,0,SwingTakeoff::PressJump,true)==jump,"takeoff jump suppressed");
        check(!swingNativeKeys(false,false,jump,SwingTakeoff::PressJump,true),"focus loss kept jumping");
        check(swingNativeKeys(true,false,jump|1,SwingTakeoff::Idle,false)==(jump|1),"walking keys lost");
    });
    test("a jump pressed in the air never reaches the game as its web zip", [] {
        constexpr uint32_t jump=swingJumpKey;
        AirJumpFilter f;
        // Pressed on the ground: the game's jump, held on into the air.
        check(f.update(jump|1,false)==(jump|1),"ground jump withheld");
        check(f.update(jump|1,true)==(jump|1),"jump held from the ground let go in the air");
        check(f.update(1,true)==1,"walking keys lost");
        // Pressed in the air: withheld until let go, a landing on the way included.
        check(f.update(jump|1,true)==1,"midair press reached the game");
        check(f.update(jump,false)==0,"midair press jumped on landing");
        check(f.update(0,false)==0,"release went wrong");
        check(f.update(jump,false)==jump,"next ground press withheld");
        f.update(0,false);
        check(f.update(jump,true)==0,"second midair press reached the game");
        check(f.update(jump,true)==0,"held midair press reached the game");
    });
    test("airborne is Spidy's flight or the game's air state, never a perch or a wall", [] {
        using game_swing::airborne;
        constexpr int64_t hz=10000000,now=50*hz;
        const auto sample=[&](uint32_t status,uint32_t owned,uint32_t grounded,uint32_t flags,int64_t age) {
            game_swing::Data d;
            d.status=status;d.owned=owned;d.grounded=grounded;d.collisionFlags=flags;
            d.qpc=static_cast<uint64_t>(now-age);
            return d;
        };
        // Flags as the October 7-8 headset reports have them.
        check(airborne(sample(2,1,0,0x2060010,0),now,hz),"Spidy's flight is not airborne");
        check(airborne(sample(2,1,0,0x2060000,0),now,hz),"Spidy's flight along a wall is not airborne");
        check(airborne(sample(1,0,0,0x2060010,hz/100),now,hz),"the game's jump or fall is not airborne");
        check(airborne(sample(1,0,0,0x3060010,0),now,hz),"the game's air state with 0x3060010 is not airborne");
        check(!airborne(sample(1,0,1,0x2060000,0),now,hz),"standing is airborne");
        check(!airborne(sample(1,0,1,0x2060010,0),now,hz),"the landing step is airborne");
        check(!airborne(sample(1,0,0,0x2060001,0),now,hz),"a perch (contact 2) is airborne");
        check(!airborne(sample(1,0,1,0x2060005,0),now,hz),"a wall crawl is airborne");
        check(!airborne(sample(1,0,0,0x2060000,0),now,hz),"a wall run is airborne");
        check(!airborne(sample(2,1,0,0x2060010,hz/5),now,hz),"a step 200 ms old is airborne");
        check(airborne(sample(2,1,0,0x2060010,-hz/100),now,hz),"a step newer than the frame is not airborne");
        check(!airborne(sample(0,1,0,0x2060010,0),now,hz),"a stopped swing is airborne");
        check(!airborne(sample(4,1,0,0x2060010,0),now,hz),"a faulted swing is airborne");
        check(!airborne(game_swing::Data{},now,hz),"no sample is airborne");
    });
    test("point launch survives release of its web through native takeoff", [] {
        SwingTakeoff t;
        auto r=t.update(0,false,true,true,true,true,{},{},{3,9,0},true);
        check(r.waiting,"unwebbed point launch ignored");
        r=t.update(80,false,true,true,false,false,{},{},{});
        check(r.jump,"point launch lost after event frame");
        t.update(140,false,false,true,false,false,{0,.3f,0},{0,6,0},{});
        r=t.update(160,false,false,true,false,false,{0,.4f,0},{0,6,0},{});
        check(r.resumed&&r.launchVelocity.x>0,"point launch momentum lost");
    });
    test("grip alone shoots a web and a held grip never shoots again", [] {
        // The October 5 session re-fired webs every 0.15 s while trigger and
        // grip stayed held, after each automatic release or miss.
        TestWorld world;world.enabled=false;auto config=inert();config.airAnchors=false;
        Swing swing(config);auto in=aimed();in.hands[0].trigger=0;
        Body actual{{0,1,0},{},true};
        swing.predictNativeStep(.01f,in,world,actual);
        check(!swing.webs()[0].attached,"miss attached");
        world.enabled=true;
        for(int i=0;i<20;++i)swing.predictNativeStep(.01f,in,world,actual);
        check(!swing.webs()[0].attached,"held grip fired again after a miss");
        in.hands[0].grip=.5f;swing.predictNativeStep(.01f,in,world,actual);
        check(!swing.webs()[0].attached,"partial release rearmed the grip");
        in.hands[0].grip=0;swing.predictNativeStep(.01f,in,world,actual);
        in.hands[0].grip=1;swing.predictNativeStep(.01f,in,world,actual);
        check(swing.webs()[0].attached,"grip press without trigger did not shoot");
        world.enabled=false;swing.predictNativeStep(.01f,in,world,actual);
        check(!swing.webs()[0].attached,"vanished surface kept the web");
        world.enabled=true;
        for(int i=0;i<20;++i)swing.predictNativeStep(.01f,in,world,actual);
        check(!swing.webs()[0].attached,"held grip fired again after losing its web");
        in.hands[0].grip=0;swing.predictNativeStep(.01f,in,world,actual);
        check(!swing.webs()[0].attached,"grip release shot a web");
    });
    test("a line grazing the anchor's facade keeps the web; a lasting wall releases it", [] {
        struct Facade : TestWorld {
            Vec3 hit{};bool blocked{};
            std::optional<RayHit> raycast(Vec3 o,Vec3 d,float distance) const override {
                if(blocked && distance>5)return RayHit{hit,{},2,true};
                return TestWorld::raycast(o,d,distance);
            }
        } world;
        Swing swing(inert());auto in=aimed();
        Body actual{{0,0,0},{},false};
        swing.predictNativeStep(.01f,in,world,actual);
        check(swing.webs()[0].attached,"no web");
        world.blocked=true;world.hit={.2f,19,0}; // a sill 1 m below the anchor
        for(int i=0;i<60;++i)swing.predictNativeStep(.01f,in,world,actual);
        check(swing.webs()[0].attached,"ledge beside the anchor released the web");
        world.hit={0,10,0}; // halfway: a wall between body and anchor
        for(int i=0;i<10;++i)swing.predictNativeStep(.01f,in,world,actual);
        check(swing.webs()[0].attached,"a passing obstruction released the web");
        world.blocked=false;swing.predictNativeStep(.01f,in,world,actual);
        world.blocked=true;
        for(int i=0;i<10;++i)swing.predictNativeStep(.01f,in,world,actual);
        check(swing.webs()[0].attached,"obstruction time was not reset by a clear line");
        bool obstructed{};
        for(int i=0;i<10;++i) {
            swing.predictNativeStep(.01f,in,world,actual);
            for(const auto& e:swing.events())obstructed|=e.kind==EventKind::Obstructed;
        }
        check(obstructed&&!swing.webs()[0].attached,"lasting wall kept the web");
    });
    // Physical webs. A building 10 m square from the origin, and a web end in
    // front of its z = 0 face: from beside its x = 10 face the line to that end
    // crosses the building once the body is past the corner between the two.
    struct Corner : Building {
        Corner(){low={0,0,0};high={10,30,10};anchor={5,20,-4};}
        std::optional<RayHit> raycast(Vec3 o,Vec3 d,float distance) const override {
            const auto box=Building::raycast(o,d,distance),point=TestWorld::raycast(o,d,distance);
            if(box&&point)return length(box->point-o)<length(point->point-o)?box:point;
            return box?box:point;
        }
        bool exists(std::uint64_t id) const override {return id==1||id==7;}
        // The left hand under the web's end, pointing up at it, its grip held.
        Input held() const {
            Input in;
            in.hands[0]={{{anchor.x,0,anchor.z},{.70710678f,0,0,.70710678f}},{0,0,0},true,0,1};
            return in;
        }
    };
    const auto physical=[]{auto c=inert();c.physical=true;return c;};
    test("a physical web bends round a building's corner and comes off it again", [physical] {
        Corner w;Swing s(physical());const auto in=w.held();
        Body body{{14,20,2},{-10,0,0},false};
        auto intent=s.predictNativeStep(1.f/60,in,w,body);
        check(s.webs()[0].attached&&!s.webs()[0].bent,"no web from beside the building");
        const float shot=s.webs()[0].length;
        bool wrapped{},cut{};
        // On past the corner: the line to the web's end crosses the building.
        for(int i=0;i<24&&!wrapped;++i) {
            body={intent.target,{-10,0,0},false};
            intent=s.predictNativeStep(1.f/60,in,w,body);
            for(const auto& e:s.events()){wrapped|=e.kind==EventKind::Wrap;cut|=e.kind==EventKind::Obstructed;}
        }
        const auto& web=s.webs()[0];
        check(wrapped&&!cut&&web.attached&&web.bent==1,"the web did not bend round the corner");
        check(length(Vec3{web.anchor.x-10,0,web.anchor.z})<.2f&&web.anchor.x>10&&web.anchor.z<0,
              "the bend is not just outside the corner's edge");
        near(length(web.end()-w.anchor),0);
        check(web.length<shot&&web.length+web.bends[0].span>=shot-.01f&&web.length+web.bends[0].span<shot+.3f,
              "the rope changed its length as it bent");
        // Drawn from the hand round that edge on to where it sticks, not to the edge.
        const auto edges=game_swing::edgesOf(web);
        check(edges.count==1&&length(edges.points[0]-web.anchor)<1e-5f,"the edge is not handed on");
        near(length(edges.end-w.anchor),0);
        // It holds the body from the corner now: flying on, the body goes round it.
        const Vec3 bend=web.anchor;const float beyond=web.length;
        float farthest{};
        for(int i=0;i<90;++i) {
            body={intent.target,intent.velocity,false};
            intent=s.predictNativeStep(1.f/60,in,w,body);
            for(const auto& e:s.events())cut|=e.kind==EventKind::Obstructed;
            farthest=std::max(farthest,length(intent.target+Vec3{0,.6f,0}-bend));
        }
        check(!cut&&s.webs()[0].attached,"the web let go behind the corner");
        check(farthest<beyond+.05f,"the body left the rope's reach round the corner");
        // Back the way it came: the rope lies straight again, and as long as it was.
        bool unwrapped{};
        body={{11,20,2},{10,0,0},false};
        for(int i=0;i<60&&!unwrapped;++i) {
            intent=s.predictNativeStep(1.f/60,in,w,body);
            for(const auto& e:s.events()){unwrapped|=e.kind==EventKind::Unwrap;cut|=e.kind==EventKind::Obstructed;}
            body={intent.target,{10,0,0},false};
        }
        check(unwrapped&&!cut&&s.webs()[0].attached&&!s.webs()[0].bent,"the web stayed bent round a corner it left");
        near(length(s.webs()[0].anchor-w.anchor),0);
        check(s.webs()[0].length>=shot-.01f&&s.webs()[0].length<shot+.3f,"the straightened rope is not as long as it was");
    });
    test("without physical webs the corner cuts the web off, as before", [] {
        Corner w;Swing s(inert());const auto in=w.held();
        Body body{{14,20,2},{-10,0,0},false};
        auto intent=s.predictNativeStep(1.f/60,in,w,body);
        check(s.webs()[0].attached,"no web");
        bool cut{},wrapped{};
        for(int i=0;i<40;++i) {
            body={intent.target,{-10,0,0},false};
            intent=s.predictNativeStep(1.f/60,in,w,body);
            for(const auto& e:s.events()){wrapped|=e.kind==EventKind::Wrap;cut|=e.kind==EventKind::Obstructed;}
        }
        check(cut&&!wrapped&&!s.webs()[0].attached,"the wall in the way kept the web");
    });
    // Arcade webs. Boxes the rays meet from either side, as the game's own do:
    // from inside one a ray meets the face it leaves by, turned along the ray.
    struct Boxes : TestWorld {
        struct Box {Vec3 low,high;std::uint64_t id;bool fixed=true;};
        std::vector<Box> boxes;
        Boxes(){anchor={0,-1000,0};}
        std::optional<RayHit> raycast(Vec3 o,Vec3 d,float distance) const override {
            auto best=TestWorld::raycast(o,d,distance);
            float nearest=best?length(best->point-o):distance;
            for(const auto& b:boxes) {
                const float from[3]{o.x,o.y,o.z},way[3]{d.x,d.y,d.z},lo[3]{b.low.x,b.low.y,b.low.z},
                    hi[3]{b.high.x,b.high.y,b.high.z};
                float enter=-1e30f,leave=1e30f;Vec3 in{},out{};bool miss=false;
                for(int a=0;a<3&&!miss;++a) {
                    Vec3 axis{};(a==0?axis.x:a==1?axis.y:axis.z)=1;
                    if(std::abs(way[a])<1e-8f){miss=from[a]<lo[a]||from[a]>hi[a];continue;}
                    float t0=(lo[a]-from[a])/way[a],t1=(hi[a]-from[a])/way[a];
                    Vec3 n0=-axis,n1=axis;
                    if(t0>t1){std::swap(t0,t1);std::swap(n0,n1);}
                    if(t0>enter){enter=t0;in=n0;}
                    if(t1<leave){leave=t1;out=n1;}
                }
                if(miss||enter>leave||leave<=0)continue;
                const bool inside=enter<=0;const float t=inside?leave:enter;
                if(t>nearest)continue;
                nearest=t;best=RayHit{o+d*t,inside?out:in,b.id,b.fixed};
            }
            return best;
        }
        bool exists(std::uint64_t id) const override {
            for(const auto& b:boxes)if(b.id==id)return b.fixed;
            return TestWorld::exists(id);
        }
    };
    const auto arcade=[physical]{auto c=physical();c.arcade=true;return c;};
    // A building's face at z = -20, and a lamp post 0.3 m thick in front of it.
    const auto street=[]{
        Boxes w;
        w.boxes={{{-30,0,-40},{30,60,-20},7},{{-.15f,0,-2.65f},{.15f,12,-2.35f},8}};
        return w;
    };
    test("an arcade web goes through a lamp post that a realistic web bends round", [physical,arcade,street] {
        // Shot at the building from beside the post and swung past it: the
        // line to the web's end crosses the post as the body passes x = 0.
        const auto run=[&](SwingConfig c){
            const auto w=street();Swing s(c);
            const Vec3 at{0,30,-20};const auto in=aimedAt({-6,4,2},at);
            Body body{{-6,4,2},{10,0,0},false};
            bool wrapped{},cut{},passed{};
            for(int i=0;i<72;++i) {
                const auto intent=s.predictNativeStep(1.f/60,in,w,body);
                for(const auto& e:s.events()) {
                    wrapped|=e.kind==EventKind::Wrap;cut|=e.kind==EventKind::Obstructed;
                    passed|=e.kind==EventKind::Pass;
                }
                body={intent.target,{10,0,0},false};
            }
            check(!cut&&s.webs()[0].attached,"the post cut the web off");
            check(body.position.x>3,"the body did not get past the post");
            return std::tuple{wrapped,passed,s.webs()[0]};
        };
        const auto [bent,through,rope]=run(physical());
        check(bent&&!through&&rope.bent>=1,"the realistic web did not bend round the post");
        check(length(Vec3{rope.anchor.x,0,rope.anchor.z+2.5f})<.6f,"the bend is not at the post");
        const auto [arcadeBent,arcadeThrough,web]=run(arcade());
        check(!arcadeBent&&arcadeThrough&&!web.bent,"the arcade web bent round the post");
        near(length(web.anchor-Vec3{0,30,-20}),0,.01f);
    });
    test("an arcade shot goes through a lamp post or a car to the building behind it", [physical,arcade,street] {
        auto w=street();
        const Vec3 from{0,4,6},facade{0,6,-20};
        const Pose aim=aimedAt(from,facade).hands[0].aim;
        const auto realistic=Swing(physical()).shot(aim,from,w);
        check(realistic.web&&realistic.web->surface==8&&realistic.passed==0,"the realistic web did not stick to the post");
        near(realistic.web->anchor.z,-2.35f,.01f);
        const auto shot=Swing(arcade()).shot(aim,from,w);
        check(shot.web&&shot.web->surface==7&&shot.passed==1,"the arcade web did not go on to the building");
        near(length(shot.web->anchor-facade),0,.01f);
        // Shot and attached: a Pass with the Attach.
        Swing s(arcade());
        Body body{from,{},false};
        s.predictNativeStep(1.f/60,aimedAt(from,facade),w,body);
        bool attached{},passed{};
        for(const auto& e:s.events()){attached|=e.kind==EventKind::Attach;passed|=e.kind==EventKind::Pass;}
        check(attached&&passed&&s.webs()[0].surface==7,"the shot through the post is not reported");
        // A car, 2 m wide and not fixed: a realistic web misses on it, an arcade web goes on past it.
        w.boxes[1]={{-1,0,-3},{1,1.6f,1},9,false};
        const Pose low=aimedAt({0,1,6},{0,2,-20}).hands[0].aim;
        check(!Swing(physical()).shot(low,{0,1,6},w).web,"a realistic web held on to a car");
        const auto past=Swing(arcade()).shot(low,{0,1,6},w);
        check(past.web&&past.web->surface==7&&past.passed==1,"the arcade web did not go past the car");
        // A post nearer the wall than thinDepth (0.2 m in front of it) is the wall's: the ray from behind it
        // starts inside the wall.
        w.boxes[1]={{-.15f,0,-19.8f},{.15f,12,-19.5f},8};
        const auto against=Swing(arcade()).shot(aim,from,w);
        check(against.web&&against.web->surface==8,"a post against a wall was let through");
    });
    test("an arcade web still bends round a building's corner", [arcade] {
        // The corner of the realistic test, its building met from either side.
        Boxes w;w.anchor={5,20,-4};w.boxes={{{0,0,0},{10,30,10},7}};
        Swing s(arcade());
        Input in;
        in.hands[0]={{{w.anchor.x,0,w.anchor.z},{.70710678f,0,0,.70710678f}},{0,0,0},true,0,1};
        Body body{{14,20,2},{-10,0,0},false};
        auto intent=s.predictNativeStep(1.f/60,in,w,body);
        check(s.webs()[0].attached,"no web from beside the building");
        bool wrapped{},cut{},passed{};
        for(int i=0;i<24&&!wrapped;++i) {
            body={intent.target,{-10,0,0},false};
            intent=s.predictNativeStep(1.f/60,in,w,body);
            for(const auto& e:s.events()) {
                wrapped|=e.kind==EventKind::Wrap;cut|=e.kind==EventKind::Obstructed;passed|=e.kind==EventKind::Pass;
            }
        }
        check(wrapped&&!cut&&!passed&&s.webs()[0].bent==1,"the arcade web went through the building");
    });
    test("arcade swings come down to the street no nearer than their own clearance", [] {
        // The street swing of the realistic test: its bottom 5.5 m under the street.
        const auto lowest=[](bool useArcade){
            Street w;auto c=inert();c.gravity=9.81f;c.taut=true;c.groundClearance=1.4f;c.arcade=useArcade;Swing s(c);
            const Vec3 from{-25,15,0};
            const auto in=aimedAt(from,w.anchor);
            Body body{from,{},false};
            float low=100,furthest=-100;
            for(int i=0;i<300;++i){
                const auto intent=s.predictNativeStep(1.f/60,in,w,body);
                body={intent.target,intent.velocity,false};
                low=std::min(low,body.position.y);furthest=std::max(furthest,body.position.x);
            }
            check(s.webs()[0].attached,"the web let go");
            check(furthest>3,"the body did not swing on past the web's end");
            return low;
        };
        const float realistic=lowest(false),arcadeLow=lowest(true);
        check(realistic>1.2f&&realistic<2,"the realistic swing is not held off the street at its clearance");
        check(arcadeLow>SwingConfig{}.arcadeClearance-.2f,"the arcade swing came nearer the street than its clearance");
        auto bad=inert();bad.groundTurn=0;
        bool threw=false;
        try{Swing broken(bad);(void)broken;}catch(const std::invalid_argument&){threw=true;}
        check(threw,"an arcade clearance with no brake accepted");
    });
    test("a physical web is held by its hand: pulling the hand pulls the body", [physical] {
        TestWorld w;Swing s(physical());auto in=aimed();in.hands[0].trigger=0;
        Body body{{0,0,0},{},false};
        auto intent=s.predictNativeStep(1.f/90,in,w,body);
        check(s.webs()[0].attached,"no web");
        // To the hand, 0.6 m above the body's centre.
        near(s.webs()[0].length,19.4f,.001f);
        // The hand comes down 0.3 m in a third of a second.
        for(int i=0;i<30;++i) {
            in.hands[0].gripRelativeToHead.y-=.01f;
            body={intent.target,intent.velocity,false};
            intent=s.predictNativeStep(1.f/90,in,w,body);
        }
        near(intent.target.y,.3f,.02f);
        near(intent.velocity.y,.9f,.15f);
        near(s.webs()[0].length,19.4f,.001f);
        // Held still, the body keeps what the pull gave it, and the rope goes slack.
        body={intent.target,intent.velocity,false};
        intent=s.predictNativeStep(1.f/90,in,w,body);
        near(intent.velocity.y,.9f,.15f);
        // A hand pushed up at the anchor pushes nobody.
        Swing still(physical());auto up=aimed();up.hands[0].trigger=0;
        Body hanging{{0,0,0},{},false};
        auto at=still.predictNativeStep(1.f/90,up,w,hanging);
        for(int i=0;i<30;++i) {
            up.hands[0].gripRelativeToHead.y+=.01f;
            hanging={at.target,at.velocity,false};
            at=still.predictNativeStep(1.f/90,up,w,hanging);
        }
        near(at.target.y,0,.001f);near(length(at.velocity),0,.001f);
        // As before, the rope is the body's: the hand moves nothing.
        Swing old(inert());auto same=aimed();same.hands[0].trigger=0;
        Body other{{0,0,0},{},false};
        auto kept=old.predictNativeStep(1.f/90,same,w,other);
        near(old.webs()[0].length,20,.001f);
        for(int i=0;i<30;++i) {
            same.hands[0].gripRelativeToHead.y-=.01f;
            other={kept.target,kept.velocity,false};
            kept=old.predictNativeStep(1.f/90,same,w,other);
        }
        near(kept.target.y,0,.001f);
    });
    test("a taut web pumps nothing for a hand that reaches toward its end and back", [physical] {
        // Hanging under the web's end, the hand reaches 0.4 m up toward it and
        // comes back, five times. A web that took in the hand's slack as well
        // ("too springy", October 15) climbed the body 0.4 m with each.
        auto c=physical();c.gravity=9.81f;c.taut=true;Swing s(c);
        TestWorld w;auto in=aimed();in.hands[0].trigger=0;
        Body body{{0,0,0},{},false};
        auto intent=s.predictNativeStep(1.f/90,in,w,body);
        check(s.webs()[0].attached,"no web");
        const auto settle=[&](int steps){
            for(int i=0;i<steps;++i){body={intent.target,intent.velocity,false};intent=s.predictNativeStep(1.f/90,in,w,body);}
        };
        settle(90);
        const float start=intent.target.y, rope=s.webs()[0].length;
        // Half a second each way, below a yank's speed: no zip.
        for(int cycle=0;cycle<5;++cycle)
            for(int i=0;i<90;++i){
                in.hands[0].gripRelativeToHead.y+=(i<45?.4f:-.4f)/45;
                settle(1);
            }
        settle(180);
        // What the body coasts on after each draw of the hand is taken in:
        // 4 cm a time, where taking in the reaches took 0.4 m.
        check(std::abs(intent.target.y-start)<.25f,"the hand's reaches pumped the body up the web");
        check(rope-s.webs()[0].length<.25f,"the hand's reaches wound the web in");
    });
    test("a snap turn carries a held rope's hand round no faster than a hand moves", [physical] {
        TestWorld w;Swing s(physical());auto in=aimed();in.hands[0].trigger=0;
        in.hands[0].gripRelativeToHead={0,-.3f,-.5f};
        Body body{{0,0,0},{},false};
        auto intent=s.predictNativeStep(1.f/90,in,w,body);
        check(s.webs()[0].attached,"no web");
        in.trackingYaw=1.5708f;
        body={intent.target,intent.velocity,false};
        intent=s.predictNativeStep(1.f/90,in,w,body);
        check(length(intent.target)<.1f&&length(intent.velocity)<.01f,"a snap turn threw the body");
    });
    test("a physical web pays out while the game walks the body, and only then", [physical] {
        TestWorld w;Swing s(physical());auto in=aimed();in.hands[0].trigger=0;
        Body body{{0,0,0},{},true};
        s.predictNativeStep(1.f/90,in,w,body);
        check(s.webs()[0].attached,"no web");
        s.payOut({0,0,0},.1f);
        near(s.webs()[0].length,19.5f,.001f);
        s.payOut({6,0,0},.1f);
        near(s.webs()[0].length,std::hypot(19.4f,6.f)+.1f,.001f);
        s.payOut({1,0,0},.1f);
        near(s.webs()[0].length,std::hypot(19.4f,6.f)+.1f,.001f);
        Swing old(inert());old.predictNativeStep(1.f/90,in,w,body);
        old.payOut({6,0,0},.1f);
        near(old.webs()[0].length,20,.001f);
    });
    test("a press with the free hand on the other hand's web takes hold of that web", [physical] {
        for(const bool ropes:{true,false}) {
            TestWorld w;Swing s(ropes?physical():inert());auto in=aimed();in.hands[0].trigger=0;
            // The right hand 0.3 m up the web from the left one, open.
            in.hands[1]={in.hands[0].aim,{0,.3f,0},true,0,0};
            Body body{{0,0,0},{},false};
            auto intent=s.predictNativeStep(1.f/90,in,w,body);
            const auto step=[&]{body={intent.target,intent.velocity,false};intent=s.predictNativeStep(1.f/90,in,w,body);};
            check(s.webs()[0].attached&&!s.webs()[1].attached&&!s.shared(),"no web to take hold of");
            const float first=s.webs()[0].length;
            step();
            check(s.onStrand(1)&&!s.onStrand(0),"the hand on the web is not seen on it");
            in.hands[1].grip=1;step();
            bool joined{},shot{};
            for(const auto& e:s.events()){joined|=e.kind==EventKind::Join&&e.hand==1;shot|=e.kind==EventKind::Attach;}
            check(joined&&!shot&&s.shared()&&!s.onStrand(1),"the press did not take hold of the web");
            check(s.webs()[1].taken&&!s.webs()[0].taken,"the hold is not marked as taken, or the shot web is");
            // The hand nearer the web's end bears the body; with one length for both, both do.
            check(s.bears(1)&&s.bears(0)==!ropes,"the wrong hand bears the body");
            near(length(s.webs()[1].anchor-w.anchor),0);
            // To the hand that took it: 0.3 m nearer the web's end where webs are ropes.
            near(s.webs()[1].length,ropes?first-.3f:first,.001f);
            near(length(intent.target),0,.001f);near(length(intent.velocity),0,.001f);
            // One hand lets go: the other has the web still, alone.
            in.hands[0].grip=0;step();
            check(!s.webs()[0].attached&&s.webs()[1].attached&&!s.shared(),"letting go with one hand lost the web");
        }
    });
    test("one web in both hands: the upper hand pulls, and hand over hand climbs it", [physical] {
        TestWorld w;Swing s(physical());auto in=aimed();in.hands[0].trigger=0;
        in.hands[1]={in.hands[0].aim,{0,.3f,0},true,0,0};
        Body body{{0,0,0},{},false};
        auto intent=s.predictNativeStep(1.f/90,in,w,body);
        const auto step=[&]{body={intent.target,intent.velocity,false};intent=s.predictNativeStep(1.f/90,in,w,body);};
        step();in.hands[1].grip=1;step();
        check(s.shared(),"no web in both hands");
        // The upper hand comes down 0.3 m: the body goes up as far, and each hand keeps its hold.
        for(int i=0;i<30;++i){in.hands[1].gripRelativeToHead.y-=.01f;step();}
        near(intent.target.y,.3f,.02f);
        near(s.webs()[0].length,19.4f,.001f);near(s.webs()[1].length,19.1f,.001f);
        // The lower hand moves nothing: it holds the slack below the other.
        for(int i=0;i<30;++i){in.hands[0].gripRelativeToHead.y-=.01f;intent.velocity={};step();}
        near(intent.target.y,.3f,.02f);
        in.hands[0].gripRelativeToHead.y+=.3f;
        // It lets go, goes 0.3 m up past the other and takes the web there.
        in.hands[0].grip=0;
        for(int i=0;i<10;++i){in.hands[0].gripRelativeToHead.y+=.03f;intent.velocity={};step();}
        check(!s.webs()[0].attached&&s.webs()[1].attached&&s.onStrand(0),"the free hand is not on the web above the other");
        in.hands[0].grip=1;intent.velocity={};step();
        check(s.shared(),"the hand did not take the web again above the other");
        near(s.webs()[0].length,18.8f,.01f);
        // And pulls in its turn.
        for(int i=0;i<30;++i){in.hands[0].gripRelativeToHead.y-=.01f;step();}
        near(intent.target.y,.6f,.04f);
    });
    test("a hand beside the web shoots its own, and a web in both hands zips once", [physical] {
        TestWorld w;Swing s(physical());auto in=aimed();in.hands[0].trigger=0;
        // The right hand 0.4 m to the side of the left one's web.
        in.hands[1]={in.hands[0].aim,{.4f,.3f,0},true,0,0};
        Body body{{0,0,0},{},false};
        auto intent=s.predictNativeStep(1.f/90,in,w,body);
        const auto step=[&]{body={intent.target,intent.velocity,false};intent=s.predictNativeStep(1.f/90,in,w,body);};
        step();
        check(!s.onStrand(1),"a hand 0.4 m off the web is on it");
        in.hands[1].grip=1;step();
        check(s.webs()[1].attached&&!s.shared()&&s.webs()[1].strand!=s.webs()[0].strand,"a press beside the web took hold of it");
        // With no reach every press shoots.
        auto never=physical();never.strandReach=0;
        Swing off(never);auto same=aimed();same.hands[0].trigger=0;same.hands[1]={same.hands[0].aim,{0,.3f,0},true,0,0};
        Body other{{0,0,0},{},false};
        auto at=off.predictNativeStep(1.f/90,same,w,other);
        other={at.target,at.velocity,false};at=off.predictNativeStep(1.f/90,same,w,other);
        check(!off.onStrand(1),"a hand is on the web of a swing that shares none");
        same.hands[1].grip=1;other={at.target,at.velocity,false};off.predictNativeStep(1.f/90,same,w,other);
        check(off.webs()[1].attached&&!off.shared(),"a swing that shares no web shared one");
        // One web, one zip, whichever hand yanks.
        Swing z(physical());auto pull=aimed();pull.hands[0].trigger=0;pull.hands[1]={pull.hands[0].aim,{0,.3f,0},true,0,0};
        Body hanging{{0,0,0},{},false};
        auto to=z.predictNativeStep(1.f/90,pull,w,hanging);
        const auto next=[&]{
            hanging={to.target,to.velocity,false};to=z.predictNativeStep(1.f/90,pull,w,hanging);
            unsigned n=0;for(const auto& e:z.events())n+=e.kind==EventKind::Zip;
            return n;
        };
        next();pull.hands[1].grip=1;next();
        check(z.shared(),"no web in both hands");
        // The lower hand's yank pulls on slack; the upper hand's zips, once.
        unsigned zips=0;
        for(int i=0;i<6;++i){pull.hands[0].gripRelativeToHead.y-=.05f;zips+=next();}
        check(zips==0,"a yank of the slack below the other hand zipped");
        for(int i=0;i<6;++i){pull.hands[1].gripRelativeToHead.y-=.05f;zips+=next();}
        check(zips==1,"the upper hand's yank did not zip");
        pull.hands[1].gripRelativeToHead.y+=.3f;next();
        for(int i=0;i<6;++i){pull.hands[1].gripRelativeToHead.y-=.05f;zips+=next();}
        check(zips==1,"the same web zipped again");
        auto bad=physical();bad.strandReach=2;
        try{Swing wide(bad);check(false,"a strand reach of 2 m accepted");}catch(const std::invalid_argument&){}
    });
    test("one web in both hands: the bearing hand lets go and the other has the web where it is", [physical] {
        // Hanging, with weight. The right hand holds the web 0.3 m above the left, which shot it.
        const auto hanging=[&](float lowerMoves){
            auto c=physical();c.gravity=9.81f;
            TestWorld w;Swing s(c);auto in=aimed();in.hands[0].trigger=0;
            in.hands[1]={in.hands[0].aim,{0,.3f,0},true,0,0};
            Body body{{0,0,0},{},false};
            auto intent=s.predictNativeStep(1.f/90,in,w,body);
            const auto step=[&]{body={intent.target,intent.velocity,false};intent=s.predictNativeStep(1.f/90,in,w,body);};
            step();in.hands[1].grip=1;step();
            check(s.shared()&&s.bears(1)&&!s.bears(0),"the upper hand does not bear the body");
            for(int i=0;i<45;++i)step();
            const float hung=intent.target.y;
            near(hung,0,.01f);
            // The lower hand moves along the web meanwhile: nothing happens.
            for(int i=0;i<20;++i){in.hands[0].gripRelativeToHead.y+=lowerMoves/20;step();}
            near(intent.target.y,hung,.01f);
            // The upper hand lets go.
            in.hands[1].grip=0;
            float lowest=hung,highest=hung;
            for(int i=0;i<45;++i){step();lowest=std::min(lowest,intent.target.y);highest=std::max(highest,intent.target.y);}
            check(s.webs()[0].attached&&!s.webs()[1].attached,"the web went with the hand that let go");
            check(lowest>hung-.03f,"the body dropped onto the lower hand's hold");
            check(highest<hung+.03f,"the body was snapped up to the lower hand's hold");
        };
        hanging(0);     // its hold 0.3 m of slack below
        hanging(-.2f);  // moved further down the web: the hold would have been 0.2 m too short
        hanging(.25f);  // moved up beside the other
    });
    test("a body that flies into a wall at an angle runs along it", [] {
        // The wall: x = 5, facing -x. The body's centre keeps 0.9 m off it.
        Building w;Swing s(walled());Input in;
        Body body{{3.5f,10,0},{10,0,-10},false};
        float nearest=100,joinedAt=0;bool joined{},on{};
        for(int i=0;i<60;++i) {
            const auto intent=s.predictNativeStep(1.f/60,in,w,body);
            body={intent.target,intent.velocity,false};
            nearest=std::min(nearest,5-body.position.x);
            for(const auto& e:s.events())on|=e.kind==EventKind::WallOn;
            if(s.wall().on&&!joined){joined=true;joinedAt=body.position.y;}
        }
        check(joined&&on&&s.wall().on,"the wall did not take the body");
        near(s.wall().normal.x,-1);near(s.wall().distance,.9f,.02f);
        check(nearest>.88f,"the body came nearer the wall than its clearance");
        near(body.velocity.x,0,.01f);
        // 10 m/s along it, raised for the 10 m/s into it, less the brake since.
        check(body.velocity.z<-9&&body.velocity.z>-12.6f,"the run lost or gained too much speed");
        // No gravity along the wall: the body keeps the little it fell at before
        // (a second of free fall is 4.9 m).
        check(joinedAt-body.position.y<1,"gravity pulled the body down the wall");
        // Without the stick or a web the run slows, stops and stays.
        body=fly(s,w,body,in,6);
        check(s.wall().on&&length(body.velocity)<.05f,"the run never came to rest on the wall");
        const float rested=body.position.y;
        body=fly(s,w,body,in,1);
        near(body.position.y,rested,.001f);
    });
    test("head on the body stops on the wall, the stick walks it and a jump leaves it", [] {
        Building w;Swing s(walled());Input in;
        Body body=fly(s,w,{{0,10,0},{12,0,0},false},in,1.5f);
        check(s.wall().on,"no wall");near(length(body.velocity),0,.05f);near(body.position.x,4.1f,.02f);
        // Pushed at the wall, the body climbs it; along it, it walks.
        in.move={1,0,0};body=fly(s,w,body,in,.6f);
        near(body.velocity.y,6,.05f);near(body.velocity.x,0,.01f);near(body.velocity.z,0,.01f);
        in.move={0,0,-1};body=fly(s,w,body,in,.8f);
        near(body.velocity.z,-6,.05f);near(body.velocity.y,0,.05f);near(body.position.x,4.1f,.02f);
        // Away from the wall is down it.
        in.move={-1,0,0};body=fly(s,w,body,in,.8f);
        near(body.velocity.y,-6,.05f);
        in.move={};body=fly(s,w,body,in,1);
        near(length(body.velocity),0,.05f);
        in.jump=true;
        const auto jump=s.predictNativeStep(1.f/60,in,w,body);
        bool jumped{};
        for(const auto& e:s.events())jumped|=e.kind==EventKind::WallJump;
        check(jumped&&!s.wall().on,"the jump did not leave the wall");
        check(jump.velocity.x<-5.5f&&jump.velocity.y>4.5f,"the jump did not go out and up");
        // Gravity is back, and the wall does not take the body again as it goes.
        body=fly(s,w,{jump.target,jump.velocity,false},in,.5f);
        check(!s.wall().on&&body.velocity.y<2&&body.position.x<2,"the body did not fly off the wall");
    });
    test("a web pulls the body off its wall and never through it", [] {
        Building w;Swing s(walled());Input in;
        Body body=fly(s,w,{{0,10,0},{12,0,0},false},in,1.5f);
        check(s.wall().on,"no wall");
        // Something carries the body off (the game reports that velocity).
        for(int i=0;i<30;++i) {
            const auto intent=s.predictNativeStep(1.f/60,in,w,{body.position,{-4,0,0},false});
            near(intent.velocity.x,-4,.01f);
            body={intent.target,intent.velocity,false};
        }
        check(!s.wall().on&&body.position.x<2.6f,"the wall kept a body that was carried off it");
        // Pushed into it, the body stays at the wall's clearance.
        Swing held(walled());
        body=fly(held,w,{{0,10,0},{12,0,0},false},in,1.5f);
        for(int i=0;i<30;++i) {
            const auto intent=held.predictNativeStep(1.f/60,in,w,{body.position,{9,0,0},false});
            check(intent.target.x<4.11f&&intent.velocity.x<.01f,"the body went into its wall");
            body={intent.target,intent.velocity,false};
        }
        check(held.wall().on,"pushing at the wall lost it");
    });
    test("up over a wall's top edge the body hops onto the roof", [] {
        Building w;Swing s(walled());Input in;
        Body body=fly(s,w,{{0,28,0},{12,0,0},false},in,1);
        check(s.wall().on,"no wall");
        in.move={1,0,0}; // up the wall, to its top at 30 m and over
        body=fly(s,w,body,in,1.2f);
        check(!s.wall().on,"the wall went on above its top");
        check(body.position.y>30&&body.velocity.x>1,"the body was not pushed on over the roof");
        in.move={};body=fly(s,w,body,in,.6f);
        check(body.position.x>5&&!s.wall().on,"the body did not come over the roof");
    });
    test("at a walk the body steps round a corner onto the next wall; a run flies off it", [] {
        Building w;Swing s(walled());Input in;
        // On the wall x = 5, three metres from its end at z = -20.
        Body body=fly(s,w,{{0,10,-17},{12,0,0},false},in,1.5f);
        check(s.wall().on,"no wall");
        in.move={0,0,-1};
        body=fly(s,w,body,in,1.2f);
        check(s.wall().on,"walking off the wall's end lost it");
        near(s.wall().normal.z,-1,.01f);near(s.wall().normal.x,0,.01f);
        check(body.position.x>5,"the body did not come round the corner");
        // Along the next face, at its clearance.
        in.move={1,0,0};body=fly(s,w,body,in,1.5f);
        check(s.wall().on,"the next wall did not hold");
        near(body.position.z,-20.9f,.03f);near(body.velocity.x,6,.05f);
        // A run past the same corner goes on in the air.
        Swing fast(walled());in.move={};
        body=fly(fast,w,{{0,10,-12},{10,0,-12},false},in,.5f);
        check(fast.wall().on&&body.velocity.z<-12,"no run");
        body=fly(fast,w,body,in,1);
        check(!fast.wall().on&&body.velocity.z<-9&&body.velocity.y<-1,"the run did not fly off the wall's end");
    });
    test("wall walking clears shallow sills before the capsule reaches them", [] {
        struct Facade : Building {
            Building sill;
            Facade() { sill.low={4.3f,12,-20};sill.high={5,12.4f,20}; }
            std::optional<RayHit> raycast(Vec3 o,Vec3 d,float reach) const override {
                const auto a=Building::raycast(o,d,reach),b=sill.raycast(o,d,reach);
                return b&&(!a||length(b->point-o)<length(a->point-o))?b:a;
            }
        } w;
        for(float dt:{1.f/90,1.f/30,.05f})for(bool down:{false,true}) {
            Swing s(walled());Input in;
            Body body=fly(s,w,{{0,down?16.f:9.f,0},{12,0,0},false},in,1.5f,dt);
            check(s.wall().on,"no starting wall");
            in.move={down?-1.f:1.f,0,0};
            for(float t=0;t<1.8f;t+=dt) {
                const auto next=s.predictNativeStep(dt,in,w,body);
                check(s.wall().on,"a shallow sill dropped the wall");
                // Upright native capsule: feet a metre below the harness,
                // head 0.8 m above it, radius 0.3 m. Its swept height must
                // not cross the sill while closer than that radius.
                const float low=std::min(body.position.y,next.target.y)-1.f;
                const float high=std::max(body.position.y,next.target.y)+.8f;
                if(low<12.4f&&high>12.f&&std::max(body.position.x,next.target.x)>=4.f)
                    throw std::runtime_error("capsule hit sill: dt="+std::to_string(dt)+
                        " down="+std::to_string(down)+" y="+std::to_string(body.position.y)+
                        " x="+std::to_string(body.position.x)+" next_x="+std::to_string(next.target.x));
                body={next.target,next.velocity,false};
            }
            check(down?body.position.y<8:body.position.y>17,"the body stalled at the sill");
            near(body.position.x,4.1f,.11f);
        }
    });
    test("wall walking follows a shallow setback but releases a deep one and a web pull", [] {
        struct Facade : Building {
            Building upper;
            Facade(float depth) { high.y=12;upper.low={5+depth,12,-20}; }
            std::optional<RayHit> raycast(Vec3 o,Vec3 d,float reach) const override {
                const auto a=Building::raycast(o,d,reach),b=upper.raycast(o,d,reach);
                return b&&(!a||length(b->point-o)<length(a->point-o))?b:a;
            }
        };
        for(float dt:{1.f/90,1.f/30,.05f}) {
            Facade w(.7f);Swing s(walled());Input in;
            Body body=fly(s,w,{{0,9,0},{12,0,0},false},in,1.5f,dt);
            in.move={1,0,0};
            for(float t=0;t<1.8f;t+=dt) {
                const auto next=s.predictNativeStep(dt,in,w,body);
                check(s.wall().on,"a shallow setback dropped the wall");
                body={next.target,next.velocity,false};
            }
            near(body.position.x,4.8f,.11f);
            in.move={};
            for(int i=0;i<60;++i) {
                body.velocity={-4,0,0};
                body=fly(s,w,body,in,dt,dt);
            }
            check(!s.wall().on,"the setback kept a body pulled off it");
            Facade deep(3);Swing off(walled());in.move={};
            body=fly(off,deep,{{0,9,0},{12,0,0},false},in,1.5f,dt);
            in.move={1,0,0};body=fly(off,deep,body,in,1.5f,dt);
            check(!off.wall().on,"a deep setback became a small step");
        }
    });
    test("wall walking clears a thin vertical facade strip in either direction", [] {
        struct Facade : Building {
            Building strip;
            Facade() { strip.low={4.3f,0,-.04f};strip.high={5,30,.04f}; }
            std::optional<RayHit> raycast(Vec3 o,Vec3 d,float reach) const override {
                const auto a=Building::raycast(o,d,reach),b=strip.raycast(o,d,reach);
                return b&&(!a||length(b->point-o)<length(a->point-o))?b:a;
            }
        } w;
        for(float dt:{1.f/90,1.f/30,.05f})for(float way:{-1.f,1.f}) {
            Swing s(walled());Input in;
            Body body=fly(s,w,{{0,10,-way*3},{12,0,0},false},in,1.5f,dt);
            in.move={0,0,way};
            for(float t=0;t<1.3f;t+=dt) {
                const auto next=s.predictNativeStep(dt,in,w,body);
                check(s.wall().on,"a thin strip dropped the wall");
                const float low=std::min(body.position.z,next.target.z)-.3f;
                const float high=std::max(body.position.z,next.target.z)+.3f;
                if(low<.04f&&high>-.04f)
                    check(std::max(body.position.x,next.target.x)<4.f,"the capsule hit the strip");
                body={next.target,next.velocity,false};
            }
            check(body.position.z*way>3,"the strip stopped sideways walking");
        }
    });
    test("a wall the body is behind is the inside of one, never held", [] {
        // The game's crawl holds the body on a wall's surface, inside its collision; a ray from there meets a
        // face turned back at the body. Held, it stood the player's view out along that backward normal,
        // through the wall (October 14: "got oob completely").
        struct Inside : Building {
            std::optional<RayHit> raycast(Vec3 o, Vec3 d, float distance) const override {
                // Every ray from inside the building meets its west face, turned the wrong way for a body in here.
                if (o.x > low.x && d.x > .5f)
                    return RayHit{{low.x, o.y, o.z}, {-1, 0, 0}, 7, true};
                return Building::raycast(o, d, distance);
            }
        } w;
        Swing s(walled());Input in;
        in.move={1,0,0};
        Body body=fly(s,w,{{5.4f,10,0},{2,0,0},false},in,.5f);
        check(!s.wall().on,"a wall the body was behind was held");
        // In front of the same face, it is a wall like any other.
        Swing front(walled());Input none;
        fly(front,w,{{0,10,0},{12,0,0},false},none,1);
        check(front.wall().on&&front.wall().distance>0,"the face from in front was not held");
    });
    test("walls leave a grounded body and a swing without walls alone", [] {
        Building w;Input in;
        Swing off;
        Body body=fly(off,w,{{0,10,0},{12,0,0},false},in,1);
        check(!off.wall().on&&body.position.x>10,"a swing without walls stopped at one");
        Swing s(walled());body={{3.9f,1,0},{4,0,0},true};
        for(int i=0;i<30;++i) {
            s.predictNativeStep(1.f/60,in,w,body);
            check(!s.wall().on,"a wall took a body that stands");
        }
        // Along a wall, not into it: none takes it.
        Swing beside(walled());
        fly(beside,w,{{3.9f,10,0},{0,0,-10},false},in,.5f);
        check(!beside.wall().on,"a wall took a body that passed it");
        // Switched off during play, the wall lets go.
        Swing live(walled());
        fly(live,w,{{0,10,0},{12,0,0},false},in,1);
        check(live.wall().on,"no wall");
        live.allowWalls(false);
        check(!live.wall().on,"walls switched off kept the body");
    });
    test("taking no new walls keeps the one the body is on and leaves the next to the game", [] {
        // What indoors asks for. Switching the walls off outright lets go of the wall at once, and the body
        // is held 0.9 m clear of it, so that is a fall straight past it - the player was dropped by it twice
        // in the headset. Taking no new ones drops nobody: he keeps what he is on, and the next wall is the
        // game's own crawl, the one hold the game itself calls standing and so the one whose vents work.
        Building w;Input in;
        Swing kept(walled());
        fly(kept,w,{{0,10,0},{12,0,0},false},in,1);
        check(kept.wall().on,"no wall to keep");
        const Vec3 held=kept.wall().normal;
        kept.takeNewWalls(false);
        check(!kept.takingNewWalls(),"the switch did not take");
        check(kept.wall().on,"taking no new walls let go of the wall it was on");
        // And it stays on it, where switching the walls off would have dropped him.
        Body body=kept.body();
        for(int i=0;i<60;++i)
            kept.predictNativeStep(1.f/60,in,w,body);
        check(kept.wall().on,"the wall it was on went anyway");
        check(length(kept.wall().normal-held)<.01f,"the wall it was on changed");
        // What it refuses is a wall OFFERED to it - the surface the game's own
        // crawl is already holding him on (game_swing offers it as soon as the
        // game turns him onto one). That is the wall the swing would otherwise
        // steal, jumping him out of the one hold whose vents and doors work.
        const Body beside{{4.5f,1.2f,0},{},false};
        Swing stolen(walled());
        for(int i=0;i<20&&!stolen.wall().on;++i) {
            stolen.offerWall({{5,1,0},{-1,0,0},0,true});
            stolen.predictNativeStep(1.f/60,in,w,beside);
        }
        check(stolen.wall().on,"an offered wall was not taken with walls on");
        Swing left(walled());
        left.takeNewWalls(false);
        for(int i=0;i<20;++i) {
            left.offerWall({{5,1,0},{-1,0,0},0,true});
            left.predictNativeStep(1.f/60,in,w,beside);
        }
        check(!left.wall().on,"the game's own wall was stolen with new walls off");
        check(!left.takingNewWalls(),"the switch did not take");
        // What it never refuses is the next wall of a climb. Refusing that drops
        // the player: the game's own crawl cannot catch a body held 0.9 m clear
        // of a wall, so a refused wall is a fall, and it was one at the vent he
        // was climbing to (the player, October 14: "i went forward, wall walked
        // to the vent and fell").
        Swing climbing(walled());
        climbing.takeNewWalls(false);
        Body flew=fly(climbing,w,{{0,10,0},{12,0,0},false},in,1);
        check(climbing.wall().on&&flew.position.x<=10,"a climb's next wall was refused, which is a fall");
        // Off outright still lets go, as it always did: that is what it means.
        Swing off(walled());
        fly(off,w,{{0,10,0},{12,0,0},false},in,1);
        off.allowWalls(false);
        check(!off.wall().on,"walls switched off kept the body");
    });
    test("held against its wall, the body is drawn in at a walk and never let go of", [] {
        // What a vent needs: the game's own collision meeting the wall, which it answers by sticking the body
        // to the wall in its crawl (the one hold the game calls standing), while the swing holds the body the
        // whole time. The usual clearance keeps that collision off the wall on purpose.
        Building w;Input in;
        Swing s(walled());
        Body body=fly(s,w,{{0,10,0},{12,0,0},false},in,1);
        check(s.wall().on,"no wall to hold");
        const float out=s.wall().distance;
        near(out,walled().wallClearance,.05f);
        s.hugWalls(true);
        check(s.huggingWalls(),"the switch did not take");
        // Drawn in, not snapped: a tenth of a second in, it is on its way.
        body=fly(s,w,body,in,.1f);
        check(s.wall().on,"holding against the wall let go of it");
        check(s.wall().distance<out-.05f&&s.wall().distance>walled().hugClearance+.05f,"it was not drawn in at a walk");
        body=fly(s,w,body,in,1);
        check(s.wall().on,"holding against the wall let go of it");
        check(s.wall().distance>=walled().hugClearance-.01f&&s.wall().distance<=walled().hugClearance+.11f,
              "it did not come to rest against the wall");
        // It does not slide down the wall meanwhile: held is held.
        const float height=body.position.y;
        body=fly(s,w,body,in,1);
        near(body.position.y,height,.05f);
        // Back out to its clearance when the hold ends, still on the wall.
        s.hugWalls(false);
        body=fly(s,w,body,in,1);
        check(s.wall().on,"ending the hold let go of the wall");
        near(s.wall().distance,walled().wallClearance,.05f);
        // A clearance out of range is no configuration.
        SwingConfig bad=walled();bad.hugClearance=bad.wallClearance+.1f;
        bool threw=false;try{Swing x(bad);}catch(const std::invalid_argument&){threw=true;}
        check(threw,"a hug clearance outside the wall clearance was accepted");
    });
    test("walked onto a wall the player is the swing's, but not in a vent", [] {
        // Since October 15, the player: "use our locomotion system only". The game's interactions work on the
        // swing's wall (game_offers), so a wall walked at is mounted as one swung onto is held.
        Building w;Swing s(walled());Input in;
        check(walled().mountFromGround,"the ground mount is off by default");
        const Body stands{{4.5f,1,0},{},true};
        in.move={1,0,0};
        bool wants{};
        for(int i=0;i<30;++i){s.predictNativeStep(1.f/60,in,w,stands);wants|=s.mounting();}
        check(wants,"walking at a wall did not mount it");
        // Crawled through a vent (allowMounts off), a duct's wall is not mounted and the game's own crawl
        // offered there is not taken from it, the stick on or not.
        Swing vent(walled());vent.allowMounts(false);
        for(int i=0;i<60;++i) {
            vent.offerWall({{5,1,0},{-1,0,0},0,true});
            vent.predictNativeStep(1.f/60,in,w,stands);
            check(!vent.mounting()&&!vent.mountBegun()&&!vent.wall().on,"a wall was mounted in a vent");
        }
        Input none;
        Body body=fly(vent,w,{{4.5f,1.2f,0},{0,11,0},false},none,.3f);
        check(!vent.wall().on,"a jump in a vent ended on the wall the crawl had offered");
        // Out of it, mounts are back.
        vent.allowMounts(true);
        wants=false;
        for(int i=0;i<30;++i){vent.predictNativeStep(1.f/60,in,w,stands);wants|=vent.mounting();}
        check(wants,"allowing mounts again did not");
        // Swinging or flying into a wall is the swing's, as before.
        Swing flying(walled());
        body=fly(flying,w,{{0,10,0},{12,0,0},false},none,1);
        check(flying.wall().on,"a body that flew into a wall was not held on it");
    });
    test("the wall the game's crawl held the body on is not taken from behind", [] {
        // A wall's face at x = 5, facing -x, and the body just inside it, as the game's crawl leaves it: a ray
        // from there toward the open side meets the face from behind, turned toward the body, as the game's
        // collision answered in the first mission's shaft (October 15, the morning build: a player held on
        // the wrong side of it until he fell out of the world).
        struct Slab : TestWorld {
            std::optional<RayHit> raycast(Vec3 o, Vec3 d, float distance) const override {
                if (std::abs(d.x) < 1e-4f)
                    return {};
                const float t = (5 - o.x) / d.x;
                if (t <= 0 || t > distance)
                    return {};
                return RayHit{o + d * t, {o.x > 5 ? 1.f : -1.f, 0, 0}, 7, true};
            }
            bool exists(std::uint64_t id) const override {
                return id == 7;
            }
        } slab;
        Input away;away.move={-1,0,0};
        const Body inside{{5.2f,10,0},{},false};
        // Without the game's crawl before it, the face is taken, as any wall pushed at would be.
        Swing plain(walled());
        plain.predictNativeStep(1.f/60,away,slab,inside);
        check(plain.wall().on&&plain.wall().normal.x>.5f,"the test's face was no wall");
        // The game's crawl held the body on that face a moment ago: its back is refused.
        Swing s(walled());
        s.supportSurface({-1,0,0},{5,10,0});
        s.supportSurface({});
        for(int i=0;i<30;++i) {
            s.predictNativeStep(1.f/60,away,slab,inside);
            check(!s.wall().on,"the crawl's wall was taken from behind");
        }
        // Its own face, from the open side, is a wall as ever.
        Swing front(walled());Input at;at.move={1,0,0};
        front.supportSurface({-1,0,0},{5,10,0});
        front.predictNativeStep(1.f/60,at,slab,{{4.4f,10,0},{},false});
        check(front.wall().on&&front.wall().normal.x<-.5f,"the crawl's own wall was refused");
        // backSeconds on, the guard is gone.
        Swing later(walled());
        later.supportSurface({-1,0,0},{5,10,0});
        later.supportSurface({});
        Input none;
        for(int i=0;i<int(Swing::backSeconds*60)+2;++i)
            later.predictNativeStep(1.f/60,none,slab,{{3,10,0},{},true});
        later.predictNativeStep(1.f/60,away,slab,inside);
        check(later.wall().on,"the guard outlived its time");
    });
    test("in the game's own wall states the swing takes no wall", [] {
        // There the body is on the wall's surface, inside its collision, and a ray from it meets the back of
        // the wall, turned toward the body: taken, it held a player behind the first mission's shaft until he
        // fell out of the world (October 15).
        Building w;Swing s(walled());Input none;
        s.gameHoldsWall(true);
        Body body=fly(s,w,{{0,10,0},{12,0,0},false},none,1);
        check(!s.wall().on,"a wall was taken while the game held the body on one of its own");
        // A wall the swing holds is left the moment the game holds the body.
        Swing on(walled());
        body=fly(on,w,{{0,10,0},{12,0,0},false},none,1);
        check(on.wall().on,"no wall to leave");
        on.gameHoldsWall(true);
        body=fly(on,w,body,none,1.f/60);
        check(!on.wall().on,"the swing kept its wall under the game's own");
        // The game's own hold over, the swing has walls again.
        on.gameHoldsWall(false);
        body=fly(on,w,{{0,10,0},{12,0,0},false},none,1);
        check(on.wall().on,"walls did not come back after the game's own");
        // But the mount's jump off the game's crawl - the walk on from it - still ends on the swing's wall.
        Swing crawl(mounted());Input walk;walk.move={0,1,0};
        const RayHit wall{{5,10,0},{-1,0,0},7,true};
        crawl.offerWall(wall);
        crawl.predictNativeStep(1.f/60,walk,w,{{5,11,0},{},true});
        check(crawl.mounting(),"the walk on asked for no jump");
        crawl.gameHoldsWall(true);
        crawl.predictNativeStep(1.f/60,none,w,{{4.7f,10.8f,0},{-7,-12,0},false});
        check(crawl.wall().on,"the mount's jump did not end on the wall");
    });
    test("walked at a wall the standing body mounts it by the game's jump", [] {
        // The wall x = 5 faces -x; the body stands against it, its centre a metre up.
        Building w;Swing s(mounted());Input in;
        const Body stands{{4.5f,1,0},{},true};
        for(int i=0;i<30;++i) {
            s.predictNativeStep(1.f/60,in,w,stands);
            check(!s.mounting(),"a body that stands at a wall wants up it without the stick");
        }
        // Walked at it for a tenth of a second, it wants up: the caller makes the jump.
        in.move={1,0,0};
        int steps=0;
        while(!s.mounting()&&steps<30){s.predictNativeStep(1.f/60,in,w,stands);++steps;}
        check(s.mounting()&&steps>=6&&steps<=8,"the mount did not come a tenth of a second into the walk");
        check(!s.wall().on,"a wall took a body that stands");
        // From the first step of that walk the caller keeps the stick from the game.
        Swing first(mounted());
        first.predictNativeStep(1.f/60,in,w,stands);
        check(first.mountBegun()&&!first.mounting(),"the walk at the wall did not begin a mount");
        // The game's jump lifts it at 11 m/s: the wall has it at once, and it
        // goes up the wall at the walk's speed, out at the wall's clearance.
        auto intent=s.predictNativeStep(1.f/60,in,w,{{4.5f,1.2f,0},{2,11,0},false});
        bool on{};
        for(const auto& e:s.events())on|=e.kind==EventKind::WallOn;
        check(on&&s.wall().on&&!s.mounting(),"the wall did not take the body as it left the ground");
        check(length(intent.velocity)<6.05f,"the jump's speed went on up the wall");
        // As its jump begins the game reports the body standing for one more
        // step, and then moving as the wall's push out to its clearance moved
        // it: the wall has it again, with no speed off it.
        s.predictNativeStep(1.f/60,in,w,{{4.45f,1.3f,0},{0,11,0},true});
        check(!s.wall().on,"a wall kept a body that stands");
        intent=s.predictNativeStep(1.f/60,in,w,{{4.4f,1.4f,0},{-4,6,0},false});
        check(s.wall().on,"the wall did not take the body again");
        check(intent.velocity.x>-.01f,"the body went on off its wall");
        Body body=fly(s,w,{intent.target,intent.velocity,false},in,.5f);
        check(s.wall().on,"the wall let the climbing body go");
        near(body.velocity.y,6,.05f);near(body.position.x,4.1f,.02f);
        // The stick let go before the body left the ground: the jump already
        // asked for still ends on the wall, at rest.
        Swing late(mounted());
        for(int i=0;i<8;++i)late.predictNativeStep(1.f/60,in,w,stands);
        check(late.mounting(),"no mount");
        in.move={};
        late.predictNativeStep(1.f/60,in,w,stands);
        check(!late.mounting(),"the mount went on without the stick");
        body=fly(late,w,{{4.5f,1.2f,0},{0,11,0},false},in,1);
        check(late.wall().on&&length(body.velocity)<.05f,"the jump did not end on its wall");
    });
    test("a stroll past a wall, a glance at it, a kerb and a far wall mount nothing", [] {
        Building w;Input in;
        const Body stands{{4.5f,1,0},{},true};
        const auto mounts=[&](const WorldQueries& world,Vec3 stick,Body body,SwingConfig c=mounted()) {
            Swing s(c);
            in.move=stick;
            bool wants{};
            for(int i=0;i<30;++i){s.predictNativeStep(1.f/60,in,world,body);wants|=s.mounting();}
            return wants;
        };
        check(mounts(w,{1,0,0},stands),"no mount head on");
        check(mounts(w,normalized({1,0,.8f}),stands),"no mount within 45 degrees of head on");
        check(!mounts(w,{0,0,1},stands),"the stick along the wall mounted it");
        check(!mounts(w,{-1,0,0},stands),"the stick away from the wall mounted it");
        check(!mounts(w,normalized({1,0,1.3f}),stands),"a glance at the wall mounted it");
        check(mounts(w,{.4f,0,0},stands),"no mount at the tilt that walks the player");
        check(!mounts(w,{.3f,0,0},stands),"a stick barely tilted mounted the wall");
        check(!mounts(w,{1,0,0},{{3,1,0},{},true}),"a wall two metres off was mounted");
        // A kerb, a car: nothing there above the body's head.
        Building kerb;kerb.high.y=1.8f;
        check(!mounts(kerb,{1,0,0},stands),"a kerb was mounted");
        check(!mounts(w,{1,0,0},stands,SwingConfig{}),"a swing without walls mounts one");
    });
    test("the game's crawl hands its wall to the swing when the stick walks on", [] {
        // The game has the body on the wall x = 5 in its crawl: its feet on
        // the wall, and reported as standing.
        Building w;Swing s(mounted());Input in;
        const Body crawls{{5,11,0},{0,2,0},true};
        const RayHit wall{{5,10,0},{-1,0,0},7,true};
        // At rest there the player is the game's: the crawl Spidy put him on
        // to use one of its interactions (game_offers) runs it, and a move of
        // the game's own off the wall - a vent's way in - is not taken.
        s.offerWall(wall);
        s.predictNativeStep(1.f/60,in,w,crawls);
        check(!s.mounting()&&!s.wall().on,"the crawl's wall asked for a jump at rest");
        Swing still(mounted());
        still.offerWall(wall);
        still.predictNativeStep(1.f/60,in,w,crawls);
        still.predictNativeStep(1.f/60,in,w,{{4.7f,10.8f,0},{-7,-12,0},false});
        check(!still.wall().on,"the game's own move off its crawl was taken from it");
        // The stick walks him on: the game's jump, then the swing's wall.
        in.move={0,1,0};
        s.offerWall(wall);
        s.predictNativeStep(1.f/60,in,w,crawls);
        check(s.mounting()&&!s.wall().on,"the crawl's wall asked for no jump");
        // The offer is for one step.
        s.predictNativeStep(1.f/60,in,w,crawls);
        check(!s.mounting(),"an offer went on by itself");
        // The game's jump throws the body off the wall, out and down: the
        // swing has it at once, at rest, and brings it out to the clearance,
        // the stick let go of by then or not.
        s.offerWall(wall);
        s.predictNativeStep(1.f/60,in,w,crawls);
        in.move={};
        s.offerWall(wall);
        s.predictNativeStep(1.f/60,in,w,crawls);
        const auto intent=s.predictNativeStep(1.f/60,in,w,{{4.7f,10.8f,0},{-7,-12,0},false});
        check(s.wall().on&&!s.mounting(),"the swing did not take the crawl's wall");
        near(length(intent.velocity),0,.01f);
        const Body body=fly(s,w,{intent.target,intent.velocity,false},in,.5f);
        near(body.position.x,4.1f,.02f);near(body.position.y,10.8f,.02f);
        // A ceiling and a wall the swing may not take are not offered.
        Swing under(mounted());
        under.offerWall({{0,3,0},{0,-1,0},7,true});
        under.predictNativeStep(1.f/60,in,w,crawls);
        check(!under.mounting(),"a ceiling was offered");
        Swing off;
        off.offerWall(wall);
        off.predictNativeStep(1.f/60,in,w,crawls);
        check(!off.mounting(),"a swing without walls took an offer");
    });
    test("a hand points at one of the game's interactions within its cone and reach", [] {
        using namespace offer_pointing;
        // A vent two metres ahead of a hand at head height, another to the right of it, one beyond reach.
        const Offer offers[]{fromGame(0x100001, {0, 1.5f, -2}, 0, true), fromGame(0x200002, {1.5f, 1.5f, -2}, 0, true),
                             fromGame(0x300003, {0, 1.5f, -9}, 0, true)};
        const Aim ahead{true, {0, 1.5f, 0}, {0, 0, -1}};
        const auto turned = [](float degrees) {
            const float a = degrees * 3.14159265f / 180;
            return Aim{true, {0, 1.5f, 0}, {std::sin(a), 0, -std::cos(a)}};
        };
        auto p = pick(ahead, offers, 3);
        check(p && p->handle == 0x100001 && p->usable, "the vent ahead was not picked");
        // Ten degrees off it still points at it; twenty does not, two metres off.
        p = pick(turned(-10), offers, 3);
        check(p && p->handle == 0x100001, "ten degrees off the vent lost it");
        check(!pick(turned(-20), offers, 3), "twenty degrees off picked something");
        p = pick(turned(36.9f), offers, 3);
        check(p && p->handle == 0x200002, "the other vent was not picked");
        // At arm's length the cone widens to the offer's size: thirty degrees off one half a metre away.
        const Offer close[]{fromGame(0x400004, {0, 1.5f, -.5f}, 0, true)};
        check(pick(turned(30), close, 1).has_value(), "a vent at arm's length needed a precise aim");
        check(!pick(turned(60), close, 1), "sixty degrees off a vent at arm's length picked it");
        // Nine metres off, an untracked hand, one the game says no to but for its range: none.
        check(!pick(ahead, offers + 2, 1), "an offer nine metres off was picked");
        check(!pick(Aim{false, {0, 1.5f, 0}, {0, 0, -1}}, offers, 3), "an untracked hand picked");
        const Offer refused[]{fromGame(0x500005, {0, 1.5f, -2}, 0x40, true),
                              fromGame(0x600006, {0, 1.5f, -2}, 0, false)};
        check(!pick(ahead, refused, 2), "an offer the game refuses was picked");
        // Too far for the game - its range alone - it is shown, to walk up to.
        const Offer far[]{fromGame(0x700007, {0, 1.5f, -2}, rangeReason, true)};
        p = pick(ahead, far, 1);
        check(p && !p->usable, "an offer out of reach was not shown");
        // A spot found for it on the wall within his reach: usable, the game's crawl goes there.
        const Offer spot[]{fromGame(0x700007, {0, 1.5f, -2}, rangeReason, true, true)};
        p = pick(ahead, spot, 1);
        check(p && p->usable, "an offer with a spot within reach was not usable");
        check(!fromGame(1, {0, 1, 0}, rangeReason | 0x40, true, true).usable &&
                  !fromGame(1, {0, 1, 0}, rangeReason, false, true).usable,
              "a spot answered more than the range");
        // Of two hands, a usable pick before one out of reach, then the nearer to its aim.
        const Pointed a{1, {}, false, .01f}, b{2, {}, true, .1f}, c{3, {}, true, .05f};
        check(either(a, b)->handle == 2 && either(b, c)->handle == 3 && either(std::nullopt, a)->handle == 1 &&
                  !either(std::nullopt, std::nullopt),
              "the hands' picks were not weighed");
    });
    test("the interact on a pointed offer asks for its use and presses the game's for the player", [] {
        using namespace offer_pointing;
        Use use;
        const std::optional<Pointed> vent = Pointed{0x100001, {0, 1, -1}, true, .05f};
        const std::optional<Pointed> farVent = Pointed{0x100001, {0, 1, -3}, false, .05f};
        Use::Game game;
        std::vector<uint32_t> asked;
        bool agree = true;
        const auto ask = [&](uint32_t handle) {
            asked.push_back(handle);
            return agree;
        };
        constexpr float dt = 1.f / 72;
        // Off Spidy's wall the interact is the game's own, pointed or not; on it, pointed at nothing usable, too.
        check(use.update(dt, true, false, vent, game, ask) && asked.empty(), "the interact off the wall was kept");
        check(!use.update(dt, false, false, vent, game, ask), "a let-go interact stayed down");
        check(use.update(dt, true, true, std::nullopt, game, ask) && asked.empty(), "a press at nothing was kept");
        use.update(dt, false, true, std::nullopt, game, ask);
        check(use.update(dt, true, true, farVent, game, ask) && asked.empty(), "a vent out of reach was asked for");
        use.update(dt, false, true, farVent, game, ask);
        // Pointed at the vent: its use is asked for, and the press stays off the game while it is held.
        check(!use.update(dt, true, true, vent, game, ask) && asked.size() == 1 && asked[0] == 0x100001,
              "the press at the vent did not ask for it");
        check(use.busy() && use.phase() == Use::Phase::asked && use.handle() == 0x100001, "no use under way");
        for (int i = 0; i < 20; ++i)
            check(!use.update(dt, true, true, vent, game, ask), "the asking press reached the game");
        // The game's crawl takes him: the game's interact waits for the game to pick the vent, and a moment
        // more - the game raises its own prompt as it picks it, and a press in that frame was lost.
        ++game.taken;
        check(!use.update(dt, false, false, std::nullopt, game, ask) && use.phase() == Use::Phase::settling,
              "the crawl taking him was missed");
        // A press of the player's own meanwhile is not the game's either.
        check(!use.update(dt, true, false, std::nullopt, game, ask), "a press during the use reached the game");
        game.best = 0x100001;
        float waited = 0;
        while (!use.update(dt, true, false, std::nullopt, game, ask) && waited < 2)
            waited += dt;
        check(std::abs(waited - Use::pickDelay) < 2 * dt, "the press did not wait for the game's prompt");
        int held = 1;
        while (use.update(dt, false, false, std::nullopt, game, ask))
            ++held;
        check(std::abs(held * dt - Use::pressSeconds) < 2 * dt, "the press was not a press");
        // The game takes it: its pick goes, and the use with it.
        game.best = 0;
        use.update(dt, false, false, std::nullopt, game, ask);
        check(!use.busy() && use.asks() == 1 && use.presses() == 1 && use.picked() == 1 && !use.retries(),
              "the use did not end");
        // The game still holding the vent after a press: pressed once more, and no more than that.
        use.update(dt, true, true, vent, game, ask);
        use.update(dt, false, true, vent, game, ask);
        ++game.taken;
        game.best = 0x100001;
        int downs = 0;
        bool was = false;
        for (int i = 0; i < 72 * 4; ++i) {
            const bool down = use.update(dt, false, false, std::nullopt, game, ask);
            downs += down && !was;
            was = down;
        }
        check(downs == 2 && !use.busy() && use.retries() == 1 && use.unanswered() == 1,
              "the press was not made once more, or more than that");
        // A game slow to pick the vent gets the press after settleSeconds anyway.
        game.best = 0;
        use.update(dt, true, true, vent, game, ask);
        use.update(dt, false, true, vent, game, ask);
        ++game.taken;
        waited = 0;
        while (!use.update(dt, false, false, std::nullopt, game, ask) && waited < 2)
            waited += dt;
        check(std::abs(waited - Use::settleSeconds) < 2 * dt && use.presses() == 4 && use.picked() == 2,
              "the press did not go out without the game's pick");
        while (use.busy())
            use.update(dt, false, false, std::nullopt, game, ask);
        // The game refusing it at once: no use, and the press stays off the game, on whose wall it does nothing.
        agree = false;
        check(!use.update(dt, true, true, vent, game, ask) && !use.busy() && use.refusals() == 1,
              "a refused use was not refused");
        use.update(dt, false, true, vent, game, ask);
        // Its crawl not taking him (the swing kept him), or no answer within patience: given up.
        agree = true;
        use.update(dt, true, true, vent, game, ask);
        use.update(dt, false, true, vent, game, ask);
        ++game.declined;
        use.update(dt, false, true, vent, game, ask);
        check(!use.busy() && use.failures() == 1, "a declined use went on");
        use.update(dt, true, true, vent, game, ask);
        use.update(dt, false, true, vent, game, ask);
        waited = 0;
        while (use.busy() && waited < 5) {
            use.update(dt, false, true, vent, game, ask);
            waited += dt;
        }
        check(!use.busy() && std::abs(waited - Use::patience) < .05f && use.failures() == 2,
              "an unanswered use did not end");
        check(use.asks() == 5 && asked.size() == 6, "the asks were not counted");
    });
    test("pushed at a wall beside it a falling body takes the wall", [] {
        Building w;Swing s(walled());Input in;
        // Down a facade a metre off it, with no speed into it.
        Body body=fly(s,w,{{4,20,0},{0,-8,0},false},in,.2f);
        check(!s.wall().on,"a wall took a body that fell past it");
        in.move={1,0,0};
        body=fly(s,w,body,in,1.5f);
        check(s.wall().on,"the stick did not take the wall");
        near(body.velocity.y,6,.1f);near(body.velocity.x,0,.01f);
        // Drifting off it as the stick takes it, the body stays.
        Swing drifting(walled());
        body=fly(drifting,w,{{4,20,0},{-2,-8,0},false},in,1);
        check(drifting.wall().on&&body.position.x>3.9f,"a body drifting off the wall left it");
        // Pushed away from it or along it, the fall goes on.
        Swing past(walled());in.move={0,0,1};
        body=fly(past,w,{{4,20,0},{0,-8,0},false},in,.5f);
        check(!past.wall().on,"the stick along a wall took it");
    });
    test("game rig rejects focus loss stale frames bad lenses and stalls", [] {
        GameTrackingRig rig;auto f=trackedFrame();
        rig.update(f,{},{0,0,-1},true);
        auto out=rig.update(f,{},{0,0,-1},true);
        check(!out.active&&!out.swing.focused&&!out.nativeKeys,"accepted stale prediction");
        f.predictedDisplayTime=2;f.focused=false;
        check(!rig.update(f,{},{0,0,-1},true).active,"accepted unfocused frame");
        f.focused=true;f.eyes[1].fov.left=0;
        check(!rig.update(f,{},{0,0,-1},true).active,"accepted invalid eye");
        f=trackedFrame();f.predictedDisplayTime=3;f.seconds=.2f;
        check(!rig.update(f,{},{0,0,-1},true).active,"accepted stalled frame");
    });
    test("game rig preserves head when recenter occurs while unfocused", [] {
        GameTrackingRig rig;auto f=trackedFrame();
        const auto before=rig.update(f,{10,20,30},{0,0,-1},true);
        f.predictedDisplayTime=2;f.focused=false;f.recentered=true;
        rig.update(f,{10,20,30},{0,0,-1},true);
        f.predictedDisplayTime=3;f.focused=true;f.recentered=false;
        f.head.position={1,1.5f,2};f.head.orientation=Quat::yaw(.7f);
        const auto after=rig.update(f,{10,20,30},{0,0,-1},true);
        check(after.active&&after.releaseWebs,"recenter event lost");
        for(int i=0;i<16;++i)near(after.head[i],before.head[i]);
    });
    test("game rig puts the head back over the feet after a break, not after a stutter", [] {
        constexpr std::int64_t ms=1'000'000;
        GameTrackingRig rig;auto f=trackedFrame();const Vec3 feet{10,20,30};
        f.predictedDisplayTime=1000*ms;
        auto out=rig.update(f,feet,{0,0,-1},true);
        near(out.headPose.position.x,10);
        // A step in the room moves the head off the body, as it should.
        f.predictedDisplayTime+=11*ms;f.head.position.x+=.4f;
        out=rig.update(f,feet,{0,0,-1},true);
        near(out.headPose.position.x,10.4f);
        // A stutter (a frame the gate held shut) keeps it there.
        f.predictedDisplayTime+=11*ms;rig.update(f,feet,{0,0,-1},false);
        f.predictedDisplayTime+=100*ms;
        out=rig.update(f,feet,{0,0,-1},true);
        near(out.headPose.position.x,10.4f);
        // Out of the play area and back, the headset unfocused for two seconds, 1.5 m walked meanwhile: the head
        // is over the feet again, facing as it was, and goes on from there.
        f.predictedDisplayTime+=11*ms;f.focused=false;rig.update(f,feet,{0,0,-1},true);
        f.predictedDisplayTime+=2000*ms;f.focused=true;f.head.position.x+=1.5f;
        out=rig.update(f,feet,{0,0,-1},true);
        near(out.headPose.position.x,10);near(out.headPose.position.z,30);
        near(length(out.headPose.orientation.rotate({0,0,-1})-Vec3{0,0,-1}),0,1e-4f);
        f.predictedDisplayTime+=11*ms;f.head.position.x+=.1f;
        out=rig.update(f,feet,{0,0,-1},true);
        near(out.headPose.position.x,10.1f);
    });
    test("game rig keeps the webs through a stutter but not a longer break", [] {
        constexpr std::int64_t ms=1'000'000;
        GameTrackingRig rig;auto f=trackedFrame();
        f.predictedDisplayTime=1000*ms;rig.update(f,{},{0,0,-1},true);
        // A game frame of over 100 ms closes the gameplay gate for a moment.
        f.predictedDisplayTime+=11*ms;
        check(!rig.update(f,{},{0,0,-1},false).active,"a closed gate kept the rig active");
        f.predictedDisplayTime+=150*ms;
        auto out=rig.update(f,{},{0,0,-1},true);
        check(out.active&&!out.releaseWebs&&out.swing.hands[0].tracked&&out.swing.hands[1].tracked,
              "a stutter released the webs");
        f.predictedDisplayTime+=11*ms;rig.update(f,{},{0,0,-1},false);
        f.predictedDisplayTime+=static_cast<std::int64_t>(controlHoldMs)*ms;
        out=rig.update(f,{},{0,0,-1},true);
        check(out.active&&out.releaseWebs&&!out.swing.hands[0].tracked,"a long break kept the webs");
    });
    test("WEB BUTTON: TRIGGER swaps each hand's web button and reel, and a change lets go of the webs", [] {
        auto f=trackedFrame();
        f.hands[0].trigger=.9f;f.hands[0].squeeze=.1f;f.hands[1].trigger=.2f;f.hands[1].squeeze=.7f;
        // The swing's grip shoots and holds the web, its trigger reels: the controllers' own by default.
        auto input=trackedSwingInput(f,{});
        near(input.hands[0].grip,.1f);near(input.hands[0].trigger,.9f);
        near(input.hands[1].grip,.7f);near(input.hands[1].trigger,.2f);
        input=trackedSwingInput(f,{},true);
        near(input.hands[0].grip,.9f);near(input.hands[0].trigger,.1f);
        near(input.hands[1].grip,.2f);near(input.hands[1].trigger,.7f);
        f.hands[0].trigger=1.5f;
        near(trackedSwingInput(f,{},true).hands[0].grip,1);
        f.hands[0].trigger=.9f;
        GameTrackingRig rig;
        rig.update(f,{},{0,0,-1},true);
        f.predictedDisplayTime=2;
        auto out=rig.update(f,{},{0,0,-1},true);
        check(out.active&&!out.releaseWebs&&out.swing.hands[0].tracked,"the webs went before the change");
        near(out.swing.hands[0].grip,.1f);
        // Switched in play: both webs let go for a frame, so a button held across the change shoots
        // nothing until it is let go; then the trigger webs.
        rig.triggerWebs(true);
        f.predictedDisplayTime=3;out=rig.update(f,{},{0,0,-1},true);
        check(out.active&&out.releaseWebs&&!out.swing.hands[0].tracked&&!out.swing.hands[1].tracked,
              "the change kept the webs");
        f.predictedDisplayTime=4;out=rig.update(f,{},{0,0,-1},true);
        check(!out.releaseWebs&&out.swing.hands[0].tracked,"the change let go of the webs twice");
        near(out.swing.hands[0].grip,.9f);near(out.swing.hands[0].trigger,.1f);
        rig.triggerWebs(true);
        f.predictedDisplayTime=5;
        check(!rig.update(f,{},{0,0,-1},true).releaseWebs,"setting the same button let go of the webs");
        // Changed in a menu (no gameplay), it lets go once play is back; reset() keeps the button.
        rig.triggerWebs(false);
        f.predictedDisplayTime=6;
        check(!rig.update(f,{},{0,0,-1},false).active,"a menu frame was active");
        f.predictedDisplayTime=7;
        check(rig.update(f,{},{0,0,-1},true).releaseWebs,"a change in a menu kept the webs");
        rig.triggerWebs(true);rig.reset();
        f.predictedDisplayTime=8;rig.update(f,{},{0,0,-1},true);
        f.predictedDisplayTime=9;out=rig.update(f,{},{0,0,-1},true);
        check(!out.releaseWebs,"the webs went again");
        near(out.swing.hands[0].grip,.9f);
    });
    test("artificial player movement does not become a hand yank", [] {
        GameTrackingRig rig;auto f=trackedFrame();rig.update(f,{},{0,0,-1},true);
        f.predictedDisplayTime=2;const auto a=rig.update(f,{},{0,0,-1},true);
        f.predictedDisplayTime=3;const auto b=rig.update(f,{20,3,-4},{0,0,-1},true);
        near(b.head[12]-a.head[12],20);near(b.head[13]-a.head[13],3);
        near(length(b.swing.hands[0].gripRelativeToHead-a.swing.hands[0].gripRelativeToHead),0);
    });
    test("game rig walks along the stock camera's axes where the head looks", [] {
        // Head turned 90 degrees left of the game camera: a stick pushed
        // forward walks to the camera's left, on the stick as on the keys.
        GameTrackingRig rig;auto f=trackedFrame();
        rig.update(f,{},{0,0,-1},true);
        f.predictedDisplayTime=2;f.head.orientation=Quat::yaw(1.5707963f);f.hands[0].stickY=1;
        auto out=rig.update(f,{},{0,0,-1},true);
        near(out.walkRight,-1);near(out.walkForward,0);
        check(out.nativeKeys==(1u<<1),"keys disagree with the stick");
        f.predictedDisplayTime=3;f.head.orientation={};f.hands[0].stickY=.5f;
        out=rig.update(f,{},{0,0,-1},true);
        near(out.walkRight,0);near(out.walkForward,.5f);
        f.predictedDisplayTime=4;f.hands[0].stickY=.1f;
        out=rig.update(f,{},{0,0,-1},true);
        check(!out.walkRight&&!out.walkForward&&!out.nativeKeys,"resting stick walked");
    });
    test("game rig lays the stick's axes into the game's crawl, so up a wall is not thrown away", [] {
        // The stick was always read on the world's horizontal axes, which on a
        // wall throw the whole of "up the wall" away - the one direction a
        // player on a wall wants most. On the game's own crawl the same two
        // axes are laid into the surface instead.
        GameTrackingRig rig;auto f=trackedFrame();const Vec3 feet{10,20,30},n{1,0,0};
        const SurfaceHold crawl{n,feet,3,true};
        f.hands[0].stickY=1;
        // On the ground they are the level ones, as they always were.
        auto out=holdFor(rig,f,feet,{},.3f);
        near(length(out.walkForwardAxis-Vec3{0,0,-1}),0,.001f);
        near(length(out.walkRightAxis-cross(Vec3{0,0,-1},Vec3{0,1,0})),0,.001f);
        // On the crawl, the view turned onto the wall: both axes lie in the
        // surface, and the stick read back along them is the player's own
        // intent turned onto that wall (swing.move). Nothing is lost.
        out=holdFor(rig,f,feet,crawl,.6f,n);
        check(out.standing,"the crawl did not turn the view");
        near(dot(out.walkForwardAxis,n),0,.001f);near(dot(out.walkRightAxis,n),0,.001f);
        near(length(out.walkForwardAxis),1,.001f);near(length(out.walkRightAxis),1,.001f);
        near(length(out.walkForwardAxis*out.walkForward+out.walkRightAxis*out.walkRight-out.swing.move),0,.002f);
        check(std::hypot(out.walkRight,out.walkForward)>.5f,"the stick came to nothing on the wall");
        // The swing's own wall is not the game's: it moves the body itself and
        // never reads this stick, so there the axes stay level.
        GameTrackingRig own;auto g=trackedFrame();g.hands[0].stickY=1;
        out=holdFor(own,g,feet,{n,feet+Vec3{0,1,0}-n*.9f,.5f,false},.6f,n);
        near(length(out.walkForwardAxis-Vec3{0,0,-1}),0,.001f);
    });
    test("game rig sends the crawl its stick along the camera's look on the wall, or its up facing it", [] {
        // Measured in the game, October 15: on a building's face (normal -z) the crawl took the stick
        // along the game camera's look laid onto the wall, and where the camera faced the wall too
        // squarely for that, along the camera's up laid onto it; right was the wall's own right of that.
        // Up the first mission's shaft the camera looks up the shaft beside the player, and the level axes
        // this replaces sent his "up" to the game as a sideways push (October 14: half his pushes there
        // under a metre a second).
        struct Case { Vec3 n, look, up, ahead, right; const char* what; };
        const Case cases[]{
            // Behind the player, facing the wall: stick up went up it, right went -x.
            {{0,0,-1},{.01f,-.09f,1},{0,1,.09f},{0,1,0},{-1,0,0},"facing the wall"},
            // Beside him on the wall, looking along it: up went along it (-x), right went down.
            {{0,0,-1},{-.91f,-.09f,-.41f},{-.08f,1,-.04f},{-1,-.1f,0},{.1f,-1,0},"along the wall"},
            // The lift shaft's own camera, looking up it: up went up, right went along the wall.
            {{-.7071068f,0,-.7071068f},{.12f,.985f,.12f},{-.696f,.174f,-.696f},{0,1,0},
             {-.7071068f,0,.7071068f},"up the shaft"},
            // A vent's shaft: the camera up it beside a wall that faces across its level look.
            {{.7071068f,0,.7071068f},{.12f,.985f,-.12f},{-.696f,.174f,.696f},{.12f,.985f,-.12f},
             {.696f,-.17f,-.696f},"up a vent's shaft"},
        };
        for (const auto& c : cases) {
            GameTrackingRig rig;auto f=trackedFrame();const Vec3 feet{10,20,30};
            const Vec3 n=normalized(c.n);
            const SurfaceHold crawl{n,feet,0,true};
            // Onto the wall from a level start, then the camera as measured.
            holdFor(rig,f,feet,{},.1f);
            GameMotionFrame out;
            for(int i=0;i<60;++i){
                ++f.predictedDisplayTime;
                out=rig.update(f,feet,normalized(c.look),true,n,true,crawl,normalized(c.up));
            }
            check(out.standing,c.what);
            near(length(out.walkForwardAxis-normalized(c.ahead)),0,.05f);
            near(length(out.walkRightAxis-normalized(c.right)),0,.05f);
            // Whatever way the player pushes along the wall, the stick says that way in those axes.
            for(const float a:{0.f,.785f,1.571f,2.356f,3.142f,4.712f}){
                f.hands[0].stickX=std::sin(a);f.hands[0].stickY=std::cos(a);
                ++f.predictedDisplayTime;
                out=rig.update(f,feet,normalized(c.look),true,n,true,crawl,normalized(c.up));
                const Vec3 sent=out.walkForwardAxis*out.walkForward+out.walkRightAxis*out.walkRight;
                near(dot(out.swing.move,n),0,.002f);
                near(length(sent-out.swing.move),0,.002f);
                check(length(out.swing.move)>.9f,"the push came to nothing on the wall");
            }
            f.hands[0].stickX=f.hands[0].stickY=0;
        }
        // The old level axes on the vent's shaft, for the record: they read "up" as a push to the side.
        const Vec3 n=normalized(Vec3{.7071068f,0,.7071068f}),level=normalized(Vec3{.12f,0,-.12f});
        near(std::abs(dot(normalized(level-n*dot(level,n)),Vec3{0,1,0})),0,.001f);
    });
    test("game rig keeps the view on the wall through a frame play cannot place, levels it for a menu", [] {
        GameTrackingRig rig;auto f=trackedFrame();const Vec3 feet{10,20,30},n{1,0,0};
        const SurfaceHold crawl{n,feet,0,true};
        auto out=holdFor(rig,f,feet,crawl,.6f,n);
        check(out.standing,"the crawl did not stand the view on the wall");
        near(out.viewTilt,1.5707963f,.001f);
        // A frame the headset repeated, then one of no game camera at all (a camera switching as the game
        // turns the hero onto a shaft's wall): October 14 levelled the view there and turned it back.
        out=rig.update(f,feet,{0,0,-1},true,n,true,crawl);
        check(!out.active&&(out.inactive&GameTrackingRig::staleDisplay),"a repeated frame was placed");
        ++f.predictedDisplayTime;
        out=rig.update(f,feet,{},true,n,true,crawl);
        check(out.active,"a frame with no camera after the start was not placed");
        near(out.viewTilt,1.5707963f,.001f);
        f.seconds=.3f;++f.predictedDisplayTime;
        out=rig.update(f,feet,{0,0,-1},true,n,true,crawl);
        check(!out.active&&(out.inactive&GameTrackingRig::badFrameTime),"a long frame was placed");
        f.seconds=1.f/90;
        out=holdFor(rig,f,feet,crawl,1.f/90,n);
        check(out.standing,"a stutter took the view off the wall");near(out.viewTilt,1.5707963f,.001f);
        // A menu: the game's own camera, and level when play comes back (then the wall turns it again).
        ++f.predictedDisplayTime;
        out=rig.update(f,feet,{0,0,-1},false,n,true,crawl);
        check(!out.active&&(out.inactive&GameTrackingRig::notPlaying),"a menu was placed");
        out=holdFor(rig,f,feet,{},1.f/90);
        near(out.viewTilt,0,.001f);
    });
    test("game rig turns the view onto a wall about the player's eyes, which go straight to the wall", [] {
        GameTrackingRig rig;auto f=trackedFrame();const Vec3 feet{10,20,30},n{1,0,0};
        auto out=holdFor(rig,f,feet,{},.1f);
        const Vec3 from=out.headPose.position;
        const SurfaceHold crawl{n,feet,0,true};
        std::vector<Vec3> path;
        for(int i=0;i<30;++i){
            out=holdFor(rig,f,feet,crawl,1.f/90,n);
            path.push_back(out.headPose.position);
        }
        // Standing on the wall: the eyes 1.7 m out from it, at the feet's height - in a sixth of a second.
        const Vec3 to=out.headPose.position;
        near(length(to-(feet+n*1.7f)),0,.002f);
        near(out.viewTilt,1.5707963f,.001f);
        near(length(path[15]-to),0,.002f);
        // On the way, on the line between: about the feet they swung round an arc 0.5 m off it.
        const Vec3 way=normalized(to-from);
        float off=0;
        for(const auto& p:path)
            off=std::max(off,length((p-from)-way*dot(p-from,way)));
        check(off<.01f,"the eyes left the straight way to the wall");
    });
    test("invalid controller cannot walk or attach a web", [] {
        auto f=trackedFrame();f.hands[0].valid=false;f.hands[0].stickY=1;
        f.hands[1].aim.position.x=std::numeric_limits<float>::quiet_NaN();
        const auto input=trackedSwingInput(f,{});
        check(input.focused&&!input.hands[0].tracked&&!input.hands[1].tracked,"invalid hand accepted");
        near(length(input.move),0);
    });
    test("game rig snap turn pivots at head and waits for stick release", [] {
        GameTrackingRig rig;auto f=trackedFrame();f.head.position.x=.4f;
        auto a=rig.update(f,{},{0,0,-1},true);
        f.predictedDisplayTime=2;f.hands[1].stickX=1;
        auto b=rig.update(f,{},{0,0,-1},true);
        for(int i=12;i<16;++i)near(a.head[i],b.head[i]);
        near(b.head[8],.5f);
        f.predictedDisplayTime=3;const auto held=rig.update(f,{},{0,0,-1},true);
        for(int i=0;i<16;++i)near(held.head[i],b.head[i]);
    });
    test("game rig stands the head off a wall the game holds the player on", [] {
        // The feet on a wall whose normal is +x: placed upright from them, the
        // head is on the wall's plane.
        GameTrackingRig rig;auto f=trackedFrame();const Vec3 feet{10,20,30};
        const auto upright=playFor(rig,f,feet,{0,1,0},.2f);
        check(!upright.onSurface,"standing counted as a wall");near(length(upright.standOff),0);
        near(upright.head[12],10);near(upright.head[13],21.7f);
        const auto first=playFor(rig,f,feet,{1,0,0},1.f/90);
        check(first.onSurface&&first.standOff.x>0&&first.standOff.x<.15f,"the head jumped off the wall");
        const auto on=playFor(rig,f,feet,{1,0,0},.6f);
        near(on.head[12],10.5f,.002f);near(on.head[13],21.7f);near(on.head[14],30);
        near(on.surfaceHeight,0);near(on.surfaceClearance,.5f,.002f);
        // Eyes, hands and the web hands go with the head; the feet stay the anchor.
        near(on.eyes[0][12]-on.head[12],-.032f);near(on.hands[0].position.x-upright.hands[0].position.x,.5f,.002f);
        near(on.swing.hands[1].aim.position.x-upright.swing.hands[1].aim.position.x,.5f,.002f);
        near(length(on.anchor-feet),0);
        // Off the wall, the head comes back over the feet.
        const auto off=playFor(rig,f,feet,{0,1,0},.6f);
        check(!off.onSurface,"standing again kept the wall");near(off.head[12],10,.002f);
    });
    test("game rig turns the view onto a wall once the player has come to rest on it", [] {
        // The swing's wall, its normal +x: the body's centre (a metre over the
        // feet) 0.9 m off it, so the wall is at x = 9.1.
        GameTrackingRig rig;auto f=trackedFrame();const Vec3 feet{10,20,30},n{1,0,0};
        SurfaceHold wall{n,feet+Vec3{0,1,0}-n*.9f,12,false};
        // A run along it never turns the view.
        auto out=holdFor(rig,f,feet,wall,.5f);
        check(!out.standing,"a run stood on the wall");near(out.viewTilt,0);
        near(length(out.standUp-Vec3{0,1,0}),0,.001f);near(length(out.floor),0);
        near(out.headPose.position.x,10);near(out.headPose.position.y,21.7f);
        // At rest the wall becomes the floor: up along its normal, the floor on
        // it under the body, a quarter turn in 0.15 s.
        wall.speed=.5f;
        out=holdFor(rig,f,feet,wall,.1f);
        check(out.standing&&out.viewTilt>.9f&&out.viewTilt<1.3f,"the view did not turn at 600 degrees a second");
        out=holdFor(rig,f,feet,wall,.3f);
        near(out.viewTilt,1.5707963f,.001f);
        near(length(out.headPose.orientation.rotate({0,1,0})-n),0,.001f);
        // The body stands with him: along the wall's normal, its floor the wall's face under the body (a
        // metre over the feet, 0.9 m behind them), and no tilt of its own on top (a flip's alone).
        near(length(out.standUp-n),0,.001f);near(dot(out.floor,n),-.9f,.002f);
        check(levelTilt(out.flipTilt),"the body was handed the wall's turn as a flip");
        near(out.headPose.position.x,9.1f+1.7f,.002f);near(out.headPose.position.y,21,.002f);
        near(out.headPose.position.z,30,.002f);
        // Hands and eyes go with the head, and hand motion turns with the view.
        near(length(out.hands[0].position-(Vec3{9.1f+1.2f,21,30}+Vec3{0,.3f,-.5f})),0,.002f);
        near(length(trackingTurn(out.swing).rotate({0,1,0})-n),0,.001f);
        // The stick goes where the player looks along the wall: turned to face
        // the wall as they stood, that is up it. The game gets it level.
        f.head.orientation=Quat::yaw(1.5707963f);f.hands[0].stickY=1;
        out=holdFor(rig,f,feet,wall,.1f);
        near(length(out.swing.move-Vec3{0,1,0}),0,.002f);
        near(out.walkRight,-1,.002f);near(out.walkForward,0,.002f);
        check(length(out.headPose.orientation.rotate({0,0,-1})-Vec3{0,1,0})<.002f,"the player does not look up the wall");
        // Walking it, however fast, the view stays.
        wall.speed=6;f.hands[0].stickY=0;f.head.orientation={};
        out=holdFor(rig,f,feet,wall,.5f);
        check(out.standing,"walking the wall turned the view back");near(out.viewTilt,1.5707963f,.001f);
        // No flip there: A is a jump off the wall.
        rig.flips(true);f.jump=true;
        out=holdFor(rig,f,feet,wall,.3f);
        near(length(out.headPose.orientation.rotate({0,1,0})-n),0,.001f);
        f.jump=false;rig.flips(false);
        // The wall lets go: level again as fast, over the feet, facing as before.
        out=holdFor(rig,f,feet,{},.4f);
        check(!out.standing,"standing on no wall");near(out.viewTilt,0,.001f);
        near(out.headPose.position.x,10,.002f);near(out.headPose.position.y,21.7f,.002f);
        near(length(out.headPose.orientation.rotate({0,0,-1})-Vec3{0,0,-1}),0,.002f);
        near(length(out.swing.tilt.rotate({0,1,0})-Vec3{0,1,0}),0,.001f);
    });
    test("the eyes keep out of what the world is built of: at once, along it, and let go slowly", [] {
        const float dt=1.f/90,nan=std::numeric_limits<float>::quiet_NaN();
        const Vec3 body{0,1,.5f},wall{0,1.6f,0},out{0,0,1};
        // A wall at z = 0 facing the player, the eyes 30 cm into it: out at once, margin off it.
        HeadClearance h;
        h.found({wall,out});
        Vec3 push=h.update(dt,body,{0,1.6f,-.3f});
        near(push.z,HeadClearance::margin+.3f,1e-4f);near(push.x,0);near(push.y,0);
        // Sliding along it, the push stays square to it.
        h.found({wall,out});
        push=h.update(dt,body,{.4f,1.5f,-.3f});
        near(push.z,HeadClearance::margin+.3f,1e-4f);near(push.x,0);near(push.y,0);
        // The head coming back out: the push lets go, never so fast that the eyes go back in.
        float last=push.z;
        for(int i=0;i<60;++i){
            h.found({wall,out});
            push=h.update(dt,body,{0,1.6f,.05f});
            check(.05f+push.z>=HeadClearance::margin-1e-4f,"the eyes went back into the wall");
            check(push.z<=last+1e-5f,"the push grew as the head left the wall");
            last=push.z;
        }
        near(push.z,HeadClearance::margin-.05f,.01f);
        for(int i=0;i<90;++i){h.found({wall,out});push=h.update(dt,body,{0,1.6f,.6f});}
        near(length(push),0,1e-3f);
        check(h.pushedFrames()>0 && h.most()>.49f,"the push was not counted");
        // An inside corner: out of both.
        HeadClearance corner;
        corner.found({{0,1.6f,0},{0,0,1}});corner.found({{0,1.6f,0},{1,0,0}});
        push=corner.update(dt,{.5f,1,.5f},{-.1f,1.6f,-.1f});
        near(push.x,HeadClearance::margin+.1f,1e-4f);near(push.z,HeadClearance::margin+.1f,1e-4f);
        // A surface found far from the eyes pushes nothing (its plane goes on past where it was found); the wall
        // that holds the player, found under his feet, reaches as far as his head.
        HeadClearance far;
        far.found({{0,-1,0},out});
        check(length(far.update(dt,body,{0,1.6f,-.3f}))==0,"a far surface pushed the eyes");
        HeadClearance held;
        held.found({{0,-1,0},out},true);
        near(held.update(dt,body,{0,1.6f,-.3f}).z,HeadClearance::margin+.3f,1e-4f);
        // Round an outside corner the wall left behind is no wall: the body is behind its plane now, so it
        // pushes nothing, where before the corner it held the eyes out of itself.
        HeadClearance round;
        const Vec3 wallA{0,1.6f,-.5f},alongA{1,0,0};
        round.found({wallA,alongA});
        near(round.update(dt,{.5f,1,-1},{-.1f,1.6f,-1}).x,HeadClearance::margin+.1f,1e-4f);
        round.reset();
        round.found({wallA,alongA});
        check(length(round.update(dt,{-1,1,.5f},{-1,1.6f,.9f}))==0,"the wall behind the corner pushed the eyes");
        // The wall that holds the player counts only while it holds him: given once, it holds the eyes off
        // that frame, and the next the push only lets go.
        HeadClearance once;
        once.found({{0,1.6f,0},out},true);
        const float first=once.update(dt,body,{0,1.6f,-.3f}).z;
        const float next=once.update(dt,body,{0,1.6f,-.3f}).z;
        check(first>.49f && next<first && next>0,"a wall no longer holding the player held the eyes off");
        // One the rays no longer find goes after keepSeconds, and the push with it.
        HeadClearance lost;
        lost.found({wall,out});
        for(int i=0;i<90;++i)push=lost.update(dt,body,{0,1.6f,-.3f});
        near(length(push),0,.01f);
        // Never behind the body's middle, along the way from it to the eyes.
        HeadClearance deep;
        deep.found({{0,1.6f,1.2f},out});
        push=deep.update(dt,{0,1.6f,1.3f},{0,1.6f,0});
        near(length(Vec3{0,1.6f,0}+push-Vec3{0,1.6f,1.3f}),0,1e-4f);
        // Nothing for what is not a surface, or eyes that are no place.
        HeadClearance bad;
        bad.found({wall,{0,0,2}});bad.found({{nan,0,0},out});
        check(length(bad.update(dt,body,{0,1.6f,-.3f}))==0,"a bad surface pushed");
        bad.found({wall,out});
        check(length(bad.update(dt,body,{nan,0,0}))==0,"eyes that are no place were pushed");
    });
    test("the head's rays go from the body to past the eyes, and from the eyes ahead, aside and up", [] {
        HeadClearance h;
        const Vec3 body{0,1,0},eyes{0,1.6f,-.3f};
        h.found({{0,1.6f,-.2f},{0,0,1}});
        const Vec3 push=h.update(1.f/90,body,eyes);
        check(push.z>0,"the eyes were not pushed");
        const auto rays=h.next({0,0,-1},{1,0,0},{0,1,0});
        // From the body toward the eyes where the headset puts them, a margin past them: a push never hides
        // the surface that asked for it.
        near(length(rays[0].origin-body),0);
        near(length(rays[0].direction-normalized(eyes-body)),0,1e-5f);
        near(rays[0].distance,length(eyes-body)+HeadClearance::margin,1e-5f);
        // The others from the eyes as pushed, each long enough to find what the eyes keep off.
        for(unsigned i=1;i<HeadClearance::rays;++i){
            near(length(rays[i].origin-(eyes+push)),0,1e-5f);
            check(rays[i].distance>HeadClearance::margin,"a ray too short to find what the eyes keep off");
        }
        near(length(rays[1].direction-Vec3{0,0,-1}),0);near(length(rays[2].direction-Vec3{1,0,0}),0);
        near(length(rays[3].direction-Vec3{-1,0,0}),0);near(length(rays[4].direction-Vec3{0,1,0}),0);
    });
    test("game rig keeps the eyes, the hands and the body out of the wall the player leans into", [] {
        // The swing's wall, its normal +x, 0.9 m from the body's centre: its face at x = 9.1. A run along it
        // (the upright view), the player leaning a metre toward it in the room.
        GameTrackingRig rig;auto f=trackedFrame();const Vec3 feet{10,20,30},n{1,0,0};
        const SurfaceHold wall{n,feet+Vec3{0,1,0}-n*.9f,12,false};
        const auto play=[&](float seconds){
            GameMotionFrame out;
            for(int i=0;i<static_cast<int>(seconds*90+.5f);++i){
                ++f.predictedDisplayTime;
                rig.bodyCentre(wall.anchor+n*.5f);
                rig.surfaceFound({wall.anchor,n},true);
                out=rig.update(f,feet,{0,0,-1},true,{0,1,0},true,wall);
            }
            return out;
        };
        auto out=play(.2f);
        near(out.headPose.position.x,10,.002f);near(length(out.headPush),0,1e-4f);
        const float hand=out.hands[0].position.x;
        f.head.position.x-=1;
        for(auto& e:f.eyes)e.pose.position.x-=1;
        out=play(.1f);
        near(out.headPose.position.x,9.1f+HeadClearance::margin,.002f);
        near(out.headPush.x,.3f,.002f);
        near(out.hands[0].position.x,hand+out.headPush.x,.002f);
        // Upright beside it again: the view comes back, not at once.
        f.head.position.x+=1;
        for(auto& e:f.eyes)e.pose.position.x+=1;
        out=play(.05f);
        check(out.headPush.x>.01f,"the push let go at once");
        out=play(1.f);
        near(length(out.headPush),0,.002f);near(out.headPose.position.x,10,.002f);
        // Without a body point the eyes go where the headset puts them.
        GameTrackingRig plain;auto g=trackedFrame();
        g.head.position.x-=1;
        for(auto& e:g.eyes)e.pose.position.x-=1;
        const auto free=holdFor(plain,g,feet,wall,.2f);
        near(length(free.headPush),0);
    });
    test("game rig turns the view as soon as the player walks the wall, not on a run the stick steers", [] {
        GameTrackingRig rig;auto f=trackedFrame();const Vec3 feet{10,20,30},n{1,0,0};
        SurfaceHold wall{n,feet+Vec3{0,1,0}-n*.9f,12,false};
        f.hands[0].stickY=1;
        auto out=holdFor(rig,f,feet,wall,.5f);
        check(!out.standing,"a steered run stood on the wall");near(out.viewTilt,0);
        // At the walk's 6 m/s with the stick held, the wall is the floor in
        // 0.15 s: a player who walked up a wall kept the upright view until
        // they let go of the stick (October 10 headset report).
        wall.speed=6;
        out=holdFor(rig,f,feet,wall,.1f);
        check(out.standing&&out.viewTilt>.9f,"walking the wall kept the view upright");
        out=holdFor(rig,f,feet,wall,.2f);
        near(out.viewTilt,1.5707963f,.001f);
        // Without the stick the same speed is a run coming to rest: upright
        // until it has.
        GameTrackingRig coasting;auto g=trackedFrame();
        out=holdFor(coasting,g,feet,wall,.3f);
        check(!out.standing,"a body coasting at a walk's speed stood on the wall");
        wall.speed=1;
        out=holdFor(coasting,g,feet,wall,.3f);
        check(out.standing,"a body at rest did not stand on the wall");
    });
    test("game rig turns a corner with the wall and keeps the heading when the wall lets go", [] {
        GameTrackingRig rig;auto f=trackedFrame();const Vec3 feet{10,20,30};
        // On the wall +x, looking along it (-z); round its end the next face is -z.
        SurfaceHold wall{{1,0,0},feet+Vec3{0,1,0}-Vec3{1,0,0}*.9f,0,false};
        auto out=holdFor(rig,f,feet,wall,.4f);
        near(length(out.headPose.orientation.rotate({0,0,-1})-Vec3{0,0,-1}),0,.002f);
        wall.normal={0,0,-1};wall.anchor=feet+Vec3{0,1,0}-wall.normal*.9f;
        out=holdFor(rig,f,feet,wall,.4f);
        near(length(out.headPose.orientation.rotate({0,1,0})-wall.normal),0,.002f);
        // Still along the wall, the way the body went round the corner.
        near(length(out.headPose.orientation.rotate({0,0,-1})-Vec3{-1,0,0}),0,.002f);
        out=holdFor(rig,f,feet,{},.4f);
        near(out.viewTilt,0,.001f);
        near(length(out.headPose.orientation.rotate({0,0,-1})-Vec3{-1,0,0}),0,.002f);
        near(length(out.headPose.orientation.rotate({0,1,0})-Vec3{0,1,0}),0,.002f);
        near(length(out.headPose.position-(feet+Vec3{0,1.7f,0})),0,.003f);
        near(out.swing.trackingYaw,1.5707963f,.002f);
        check(out.swing.tilt.x==0&&out.swing.tilt.y==0&&out.swing.tilt.z==0,"a level player kept a tilt");
    });
    test("game rig stands on the game's wall crawl at once, or stands off it with the view kept upright", [] {
        // The game's crawl: the actor's feet on the wall (+x), its up along it.
        const Vec3 feet{10,20,30},n{1,0,0};
        const SurfaceHold crawl{n,feet,3,true};
        GameTrackingRig rig;auto f=trackedFrame();
        holdFor(rig,f,feet,{},.2f);
        auto out=holdFor(rig,f,feet,crawl,.6f,n);
        check(out.standing,"the crawl did not turn the view");near(out.viewTilt,1.5707963f,.001f);
        near(length(out.headPose.position-(feet+n*1.7f)),0,.003f);near(length(out.standOff),0,.002f);
        // STAND ON WALLS off: upright, and stood off the wall as before.
        GameTrackingRig upright;upright.standOnWalls(false);auto g=trackedFrame();
        holdFor(upright,g,feet,{},.2f);
        out=holdFor(upright,g,feet,crawl,.6f,n);
        check(!out.standing&&out.onSurface,"the view turned with STAND ON WALLS off");near(out.viewTilt,0);
        near(out.headPose.position.x,10.5f,.002f);near(out.headPose.position.y,21.7f,.002f);
        // Switched off on the wall, the view turns back; reset() keeps the setting.
        rig.standOnWalls(false);
        out=holdFor(rig,f,feet,crawl,.6f,n);
        near(out.viewTilt,0,.001f);near(out.headPose.position.x,10.5f,.002f);
        upright.reset();
        g.predictedDisplayTime+=1000;
        out=holdFor(upright,g,feet,crawl,.6f,n);
        check(!out.standing,"reset() switched STAND ON WALLS back on");
    });
    test("the head never goes through what is over the player: a vent's roof", [] {
        // The player's eyes are 1.7 m up, as trackedFrame has them.
        GameTrackingRig rig;auto f=trackedFrame();const Vec3 feet{10,20,30};
        auto out=holdFor(rig,f,feet,{},.4f);
        near(out.headPose.position.y-feet.y,1.7f,.01f);near(out.headDrop,0);
        // A duct a metre high: the head stops headClearance short of its roof,
        // where it stood out through it (the player, October 14, inside the
        // first mission's vent: "the camera clips through the top of it").
        rig.headroom(1);
        out=holdFor(rig,f,feet,{},.4f);
        near(out.headPose.position.y-feet.y,1.f-headClearance,.01f);
        near(out.headDrop,1.7f-(1.f-headClearance),.01f);
        // Never closer to his feet than a crouch, however low the roof.
        rig.headroom(.2f);
        out=holdFor(rig,f,feet,{},.4f);
        near(out.headPose.position.y-feet.y,leastHeadroom,.01f);
        // A roof over his own head changes nothing, and neither does none.
        rig.headroom(2.5f);
        out=holdFor(rig,f,feet,{},.4f);
        near(out.headPose.position.y-feet.y,1.7f,.01f);near(out.headDrop,0);
        rig.headroom(0);
        out=holdFor(rig,f,feet,{},.4f);
        near(out.headPose.position.y-feet.y,1.7f,.01f);
        // On a wall it is one rule along the wall's own normal: the head stops
        // short of the duct's far side as it does of its roof.
        const Vec3 n{1,0,0};
        rig.headroom(1);
        out=holdFor(rig,f,feet,{n,feet,3,true},.8f,n);
        check(out.standing,"the crawl did not turn the view");
        near(out.headPose.position.x-feet.x,1.f-headClearance,.02f);
        near(out.headPose.position.y-feet.y,0,.02f);
        // A reading that is no number is no reading.
        rig.headroom(std::numeric_limits<float>::quiet_NaN());
        out=holdFor(rig,f,feet,{},.4f);
        near(out.headPose.position.y-feet.y,1.7f,.01f);
    });
    test("game rig head stays still while the actor rocks on the wall", [] {
        // Measured in the game (reports/wall-crawl.json): on the wall the
        // actor's up leans up to 14 degrees off the wall's normal over a few
        // frames and snaps back to it.
        GameTrackingRig rig;auto f=trackedFrame();const Vec3 feet{10,20,30};
        const auto settled=playFor(rig,f,feet,{1,0,0},.6f);
        float lowest=1e9f,highest=-1e9f,nearest=1e9f;
        for(int cycle=0;cycle<6;++cycle)
            for(int k=0;k<=6;++k){
                const float a=.0407f*static_cast<float>(k);
                const auto out=playFor(rig,f,feet,{std::cos(a),std::sin(a),0},1.f/90);
                lowest=std::min(lowest,out.head[13]);highest=std::max(highest,out.head[13]);
                nearest=std::min(nearest,out.head[12]);
            }
        near(highest-lowest,0);near(settled.head[12]-nearest,0);near(nearest,10.5f,.002f);
    });
    test("game rig head on a wall moves freely within its clearance", [] {
        GameTrackingRig rig;auto f=trackedFrame();const Vec3 feet{10,20,30};
        playFor(rig,f,feet,{1,0,0},.6f);
        // A lean toward the wall comes closer, up to the minimum clearance.
        f.head.position.x=-.2f;f.eyes[0].pose.position.x=-.232f;f.eyes[1].pose.position.x=-.168f;
        auto out=playFor(rig,f,feet,{1,0,0},.3f);
        near(out.head[12],10.3f,.002f);near(out.surfaceClearance,.3f,.002f);
        f.head.position.x=-.4f;f.eyes[0].pose.position.x=-.432f;f.eyes[1].pose.position.x=-.368f;
        out=playFor(rig,f,feet,{1,0,0},.3f);
        near(out.surfaceClearance,minWallClearance,.002f);
        // Leaning back, the wall does not follow.
        f.head.position.x=0;f.eyes[0].pose.position.x=-.032f;f.eyes[1].pose.position.x=.032f;
        out=playFor(rig,f,feet,{1,0,0},.3f);
        near(out.head[12],10.65f,.002f);
    });
    test("game rig turns about the head on a wall and keeps it on a recenter", [] {
        GameTrackingRig rig;auto f=trackedFrame();const Vec3 feet{10,20,30};
        const auto before=playFor(rig,f,feet,{1,0,0},.6f);
        f.hands[1].stickX=1;auto turned=playFor(rig,f,feet,{1,0,0},1.f/90);
        f.hands[1].stickX=0;turned=playFor(rig,f,feet,{1,0,0},.3f);
        near(turned.head[12],before.head[12],.002f);near(turned.head[14],before.head[14],.002f);
        f.recentered=true;f.head.position={.3f,1.7f,.2f};
        const auto recentered=playFor(rig,f,feet,{1,0,0},1.f/90);
        f.recentered=false;
        near(recentered.head[12],turned.head[12],.002f);near(recentered.head[14],turned.head[14],.002f);
    });
    test("game rig puts the head under a ceiling and leaves slopes alone", [] {
        GameTrackingRig rig;auto f=trackedFrame();const Vec3 feet{10,20,30};
        auto out=playFor(rig,f,feet,{0,-1,0},.8f);
        near(out.head[13],19.5f,.003f);near(out.surfaceClearance,.5f,.003f);
        out=playFor(rig,f,feet,{0,1,0},.8f);
        near(out.head[13],21.7f,.003f);
        // 30 degrees from upright is no wall; 40 is one only for a player already on a wall.
        const float s30=.5f,c30=.8660254f,s40=.6427876f,c40=.7660444f;
        out=playFor(rig,f,feet,{s30,c30,0},.3f);
        check(!out.onSurface,"a slope counted as a wall");near(length(out.standOff),0);
        out=playFor(rig,f,feet,{s40,c40,0},.1f);
        check(!out.onSurface,"40 degrees from standing counted as a wall");
        playFor(rig,f,feet,{1,0,0},.1f);
        out=playFor(rig,f,feet,{s40,c40,0},.1f);
        check(out.onSurface,"40 degrees on a wall let go of it");
    });
    test("aimed hand attaches to actual surface", [] {
        TestWorld w;
        Swing s(inert());
        s.reset({{0, 0, 0}, {}, false});
        s.update(1.f / 90, aimed(), w);
        check(s.webs()[0].attached, "missing web");
        check(!s.webs()[0].airAnchor,"real surface replaced by sky anchor");
        near(s.webs()[0].length, 20);
    });
    test("clear sky attaches at maximum hand reach and remains fixed while reeling", [] {
        TestWorld w;
        w.enabled = false;
        Swing s(inert());
        auto in = aimed();
        in.hands[0].aim.position={2,2,3};
        Body actual{{2,1,3},{},true};
        s.predictNativeStep(.01f,in,w,actual);
        auto web=s.webs()[0];
        check(web.attached&&web.airAnchor,"sky shot did not attach");
        near(length(web.anchor-in.hands[0].aim.position),100);
        near(web.anchor.y,102);near(web.length,101);
        in.hands[0].trigger=0;s.predictNativeStep(.01f,in,w,actual);
        in.hands[0].trigger=1;in.hands[0].aim.position.x=5;
        const auto pull=s.predictNativeStep(.02f,in,w,actual);
        check(pull.target.y>actual.position.y,"sky anchor did not reel player up");
        check(s.webs()[0].attached,"sky anchor required a world body");
        near(length(s.webs()[0].anchor-web.anchor),0);
        in.hands[0].grip=0;s.predictNativeStep(.01f,in,w,actual);
        check(!s.webs()[0].attached,"sky anchor ignored grip release");
    });
    test("sky anchoring can be disabled for surface-only simulations", [] {
        TestWorld w;w.enabled=false;
        auto config=inert();config.airAnchors=false;Swing s(config);
        s.update(.01f,aimed(),w);
        check(!s.webs()[0].attached,"disabled sky attachment accepted");
    });
    test("webs in open air switch off and on during play; a held web keeps its anchor", [] {
        TestWorld w;w.enabled=false;
        Swing s(inert());s.reset({{0,0,0},{},false});
        const auto in=aimed();auto open=in;open.hands[0].grip=0;
        s.update(.01f,in,w);
        check(s.webs()[0].attached&&s.webs()[0].airAnchor,"open air did not hold the web");
        const Vec3 anchor=s.webs()[0].anchor;
        s.allowAirAnchors(false);
        check(!s.config().airAnchors,"the switch did not reach the configuration");
        const auto preview=s.shot(in.hands[0].aim,s.body().position,w);
        check(!preview.web&&!preview.hit,"switched off, open air still previewed a web");
        s.update(.01f,in,w);
        check(s.webs()[0].attached&&length(s.webs()[0].anchor-anchor)<1e-4f,"switching off let go of a held web");
        s.update(.01f,open,w);s.update(.01f,in,w);
        check(!s.webs()[0].attached,"switched off, a press still held in open air");
        w.enabled=true;
        s.update(.01f,open,w);s.update(.01f,in,w);
        check(s.webs()[0].attached&&!s.webs()[0].airAnchor,"switched off, a surface no longer held the web");
        w.enabled=false;s.allowAirAnchors(true);
        s.update(.01f,open,w);s.update(.01f,in,w);
        check(s.webs()[0].attached&&s.webs()[0].airAnchor,"switched on again, open air did not hold the web");
    });
    test("the VR settings' weight changes the swing's gravity during play", [] {
        TestWorld w;w.enabled=false;
        auto config=inert();config.gravity=vr_settings::gravity(vr_settings::Values{}.weight);
        Swing s(config);
        const Input none;
        const Body flying{{0,50,0},{},false};
        near(config.gravity,9.81f);
        near(s.predictNativeStep(.02f,none,w,flying).velocity.y,-9.81f*.02f);
        s.setGravity(vr_settings::gravity(150));
        near(s.config().gravity,14.715f);
        near(s.predictNativeStep(.02f,none,w,flying).velocity.y,-14.715f*.02f);
        for(const float bad:{std::numeric_limits<float>::quiet_NaN(),-1.f,std::numeric_limits<float>::infinity()}){
            bool threw=false;
            try{s.setGravity(bad);}catch(const std::invalid_argument&){threw=true;}
            check(threw,"an invalid gravity accepted");
        }
        near(s.config().gravity,14.715f);
    });
    test("pocket draw: a grip squeezed at a back pocket, brought up in front, opens the phone there", [] {
        const Pose head{{0,1.7f,0},{}};
        // A back pocket 20 cm behind the eyes; a hand hanging at the side, about 8 cm.
        const Pose pocket{{.2f,.9f,.2f},{}}, front{{.1f,1.25f,-.3f},{}}, hip{{.2f,.9f,-.2f},{}},
            side{{.25f,.95f,.08f},{}};
        bool tracked[2]{true,true};
        const auto step=[&](PocketDraw& d,const Pose& right,float squeeze,float seconds=1.f/90,bool play=true,
                            const Pose* eyes=nullptr){
            const Pose grips[2]{Pose{{-.3f,1.f,0},{}},right};
            const float squeezes[2]{0,squeeze};
            return d.update(seconds,play,eyes?*eyes:head,grips,tracked,squeezes);
        };
        PocketDraw d;
        check(!step(d,pocket,1),"the squeeze at the pocket opened it at once");
        check(d.hands()==2u,"the drawing grip is not the draw's: it would shoot a web");
        check(step(d,front,1)&&d.draws()==1,"brought up in front, it did not open");
        check(length(d.hand()-front.position)<1e-4f,"it did not open at the hand");
        check(d.hands()==2u,"the grip was let go to the swing while still squeezed");
        step(d,front,0);
        check(d.hands()==0u,"let go of, the grip stayed the draw's");
        // A squeeze in front of the hip, or with the arm hanging at the side, is a web's, not a draw.
        check(!step(d,hip,1)&&d.hands()==0u,"a squeeze at the front of the hip began a draw");
        step(d,front,1);
        check(d.draws()==1,"a web's squeeze brought up opened the phone");
        step(d,front,0);
        check(!step(d,side,1)&&d.hands()==0u,"a squeeze with the arm hanging at the side began a draw");
        step(d,side,0);
        // Nor with the head turned 30 degrees the other way for a moment: the
        // pocket is the body's, which a glance does not turn.
        const Pose glance{{0,1.7f,0},Quat::yaw(.52f)};
        for(int i=0;i<30;++i)step(d,side,0,1.f/90,true,&glance);
        check(!step(d,side,1,1.f/90,true,&glance)&&d.hands()==0u,"a glance over the shoulder made a pocket of the side");
        step(d,side,0,1.f/90,true,&glance);
        for(int i=0;i<300;++i)step(d,side,0);
        check(std::abs(d.yaw())<.05f,"the body did not turn back with the head");
        // Let go on the way, nothing opens.
        step(d,pocket,1);
        step(d,pocket,0);
        check(!step(d,front,1)&&d.draws()==1,"a draw let go of on the way opened it");
        step(d,front,0);
        // Too slow, nothing opens.
        step(d,pocket,1);
        for(int i=0;i<20;++i)step(d,pocket,1,.1f);
        check(!step(d,front,1)&&d.draws()==1,"a draw slower than drawSeconds opened it");
        step(d,front,0);
        check(!step(d,pocket,1,1.f/90,false)&&d.hands()==0u,"outside VR play a draw began");
        step(d,pocket,0);
        // A hand the headset loses behind the back keeps its grip the draw's, and the draw goes on.
        step(d,pocket,1);
        tracked[1]=false;
        for(int i=0;i<20;++i)step(d,Pose{},1);
        check(d.hands()==2u,"a hand lost behind the back let go of its grip");
        tracked[1]=true;
        check(step(d,front,1)&&d.draws()==2,"a draw through a hand lost behind the back did not open the phone");
        step(d,front,0);
        // A press with the hand just lost begins a draw where it was last seen.
        step(d,pocket,0);
        tracked[1]=false;
        check(!step(d,Pose{},1)&&d.hands()==2u,"a press with the hand lost at the pocket began no draw");
        tracked[1]=true;
        check(step(d,front,1)&&d.draws()==3,"a draw begun with the hand lost did not open the phone");
        step(d,front,0);
        // A game hitch on the way (a moment out of play) does not end a draw.
        step(d,pocket,1);
        step(d,pocket,1,1.f/90,false);
        step(d,pocket,1,1.f/90,false);
        check(step(d,front,1)&&d.draws()==4,"a hitch on the way ended the draw");
        step(d,front,0);
        check(d.drewWith()==1,"the right hand's draw was not the right hand's");
        const auto f=phoneFrameAt(head,front.position);
        check(dot(f.out,normalized(head.position-f.centre))>.999f&&f.up.y>.5f&&std::abs(dot(f.up,f.out))<1e-4f,
              "the held phone does not face the eyes with its top up");
        check(dot(f.right,cross(f.up,f.out))>.999f&&f.right.x>.5f,
              "the held phone's right is not the viewer's right: it would show its back");
        check(length(f.centre-front.position)<.1f,"the held phone is not at the hand");
    });
    test("phone hold: a control changes hands only once let go of, so nothing sees a press nobody made", [] {
        PhoneHold hold;
        XrFrame f, mine;
        const auto at=[&](bool live,uint32_t buttons,float rightGrip=0,bool jump=false){
            f=XrFrame{};
            f.buttons=buttons;
            f.jump=jump;
            f.hands[1].squeeze=rightGrip;
            hold.update(live,f,mine);
        };
        // A web held as the phone comes out keeps holding until let go of; then the phone has the grip.
        at(false,0,1);
        at(true,0,1);
        check(f.hands[1].squeeze==1&&mine.hands[1].squeeze==0,"the grip holding a web was taken as the phone came out");
        check(hold.any(),"the phone did not take the controls let go of");
        at(true,0,0);
        at(true,0,1);
        check(f.hands[1].squeeze==0&&mine.hands[1].squeeze==1,"a grip squeezed with the phone out reached the game");
        // The B that puts the phone away stays the phone's until let go of, and so does a grip held meanwhile.
        at(true,buttonB,1);
        at(false,buttonB,1);
        check(!(f.buttons&buttonB)&&f.hands[1].squeeze==0,"the B that put the phone away, or a grip held, reached the game");
        at(false,0,0);
        check(!hold.any(),"let go of, the controls did not go back to the game");
        at(false,buttonB,1);
        check((f.buttons&buttonB)&&f.hands[1].squeeze==1,"the game did not get its controls back");
        // A B held from the pause menu back into play stays the game's: the phone sees none.
        at(true,buttonB);
        check((f.buttons&buttonB)&&!(mine.buttons&buttonB),"a B held into the phone's play was the phone's");
        // The menu button is always the game's; A is the frame's jump too.
        at(true,buttonMenu);
        check((f.buttons&buttonMenu)!=0,"the phone took the menu button");
        at(true,buttonA,0,true);
        check(!f.jump&&!(f.buttons&buttonA)&&mine.jump&&(mine.buttons&buttonA),"A was not the phone's, jump and all");
    });
    test("phone: a draw opens it in the hand once VR shows play, where it stays until B puts it away", [] {
        Phone p;
        const Pose head{{0,1.7f,0},{}};
        Pose grips[2]{Pose{{-.2f,1.1f,-.3f},{}},Pose{{.2f,1.1f,-.3f},{}}};
        bool tracked[2]{true,true};
        const float frame=1.f/90;
        check(!p.update(frame,false,head,grips,tracked,1,grips[1].position)&&!p.open(),
              "a draw opened the phone outside VR play");
        check(!p.update(frame,true,head,grips,tracked)&&!p.open(),"the phone opened with no draw");
        // Drawn from a pocket it opens in the drawing hand and follows it; a hand lost leaves it where it was.
        check(p.update(frame,true,head,grips,tracked,1,grips[1].position)&&p.open()&&p.live()&&p.hand()==1,
              "a draw did not open the phone in its hand");
        grips[1].position={.25f,1.2f,-.35f};
        p.update(frame,true,head,grips,tracked);
        const Vec3 held=p.place().centre;
        check(length(held-phoneFrameAt(head,grips[1].position).centre)<1e-4f,"the drawn phone did not follow its hand");
        tracked[1]=false;
        grips[1].position={};
        p.update(frame,true,head,grips,tracked);
        check(length(p.place().centre-held)<1e-4f,"a hand the headset lost carried the phone off");
        // A draw with the phone out takes nothing from the hand holding it.
        check(!p.update(frame,true,head,grips,tracked,0,grips[0].position)&&p.hand()==1,
              "a second draw took the phone out of its hand");
        // Out of play (a pause, a calibration) it is open but not live; back in play, live.
        p.update(frame,false,head,grips,tracked);
        check(p.open()&&!p.live(),"out of play the phone stayed live");
        p.update(frame,true,head,grips,tracked);
        check(p.live(),"back in play the phone was not live");
        // B on the home screen puts it away, and the game never sees that B.
        XrFrame f;
        p.take(f,0);
        f=XrFrame{};
        f.buttons=buttonB;
        check(p.take(f,0).action==PhoneAction::close&&!p.open()&&!p.live(),"B on the home screen did not put it away");
        check(!(f.buttons&buttonB),"the B that put the phone away reached the game");
    });
    test("phone: home screen apps, MUSIC's buttons, GEAR's scroll; A, B, stick and triggers once per press", [] {
        PhoneControls c;
        // Each opening counts everything as held: the press that opened it presses nothing until let go of.
        c.update(0,0,false,false);
        c.reset();
        check(!c.update(0,0,true,false).pressed,"the opening press pressed something");
        auto e=c.update(0,0,false,false);
        check(!e.pressed&&!e.x&&!e.y,"nothing happened, yet something stepped");
        e=c.update(1,.2f,false,false);
        check(e.x==1&&e.y==0,"right was not a step right");
        check(!c.update(1,0,false,false).x,"a held stick kept stepping");
        c.update(0,0,false,false);
        e=c.update(.1f,-.9f,false,false);
        check(e.y==-1&&e.x==0,"down was not a step down");
        c.update(0,0,false,false);
        check(c.update(0,0,true,false).pressed&&!c.update(0,0,true,false).pressed,"A pressed never, or twice held");
        check(c.update(0,0,false,true).closed,"B did not count");
        // The triggers: the left a step down, the right a step up, once per pull; each held
        // as the phone opens counts nothing.
        PhoneControls t;
        t.update(0,0,false,false,0,0);
        t.reset();
        check(t.update(0,0,false,false,1,0).volume==0,"a left trigger held as it opened stepped the volume");
        t.update(0,0,false,false,0,0);
        t.reset();
        check(t.update(0,0,false,false,0,1).volume==0,"a right trigger held as it opened stepped the volume");
        t.update(0,0,false,false,0,0);
        check(t.update(0,0,false,false,1,0).volume==-1,"the left trigger did not step it down");
        check(t.update(0,0,false,false,1,0).volume==0,"a held left trigger stepped it again");
        t.update(0,0,false,false,0,0);
        check(t.update(0,0,false,false,0,.9f).volume==1,"the right trigger did not step it up");
        // The screens.
        PhoneUi ui;
        ui.reset();
        using E=PhoneControls::Event;
        const auto step=[&](E ev,int rows=0){return ui.update(ev,rows);};
        check(ui.screen()==PhoneScreen::home&&ui.app()==0,"it did not open home, MUSIC picked");
        step(E{0,-1});
        check(ui.app()==1,"down did not pick MAP");
        step(E{0,1});step(E{0,1});
        check(ui.app()==2,"up from MUSIC did not wrap to GEAR");
        step(E{0,0,true});
        check(ui.screen()==PhoneScreen::gear,"A did not open GEAR");
        step(E{0,-1},9);step(E{0,-1},9);step(E{0,-1},9);step(E{0,-1},9);
        check(ui.gearTop()==9-PhoneUi::gearShown,"GEAR scrolled past its last row");
        step(E{0,1},9);
        check(ui.gearTop()==9-PhoneUi::gearShown-1,"up did not scroll GEAR back");
        check(step(E{0,0,false,true})==PhoneAction::none&&ui.screen()==PhoneScreen::home,"B in an app did not go home");
        step(E{0,1});step(E{0,1});
        step(E{0,0,true});
        check(ui.screen()==PhoneScreen::music&&ui.button()==PhoneButton::play,"MUSIC did not open on PLAY");
        step(E{1});
        check(step(E{0,0,true})==PhoneAction::next,"right then A did not press NEXT");
        step(E{0,-1});
        check(ui.button()==PhoneButton::autoplay&&step(E{0,0,true})==PhoneAction::autoplay,"down did not reach AUTOPLAY");
        step(E{0,1});
        check(ui.button()==PhoneButton::play,"up from AUTOPLAY did not go back to PLAY");
        step(E{0,0,false,true});
        check(step(E{0,0,false,true})==PhoneAction::close,"B at home did not put the phone away");
        // The songs' names: the font's letters, accents taken off, and TRACK and its number for none.
        check(phoneSongName("C:\\Music\\my_song.mp3",0)=="my song","a song's name kept its folder, extension or underscore");
        check(phoneSongName("Beyonc\xC3\xA9 - Halo.mp3",0)=="Beyonce - Halo","an accented letter was not its letter");
        check(phoneSongName("Beyonce\xCC\x81.mp3",0)=="Beyonce","a name in NFD kept its accent");
        check(phoneSongName("Stra\xC3\x9F" "e.mp3",0)=="Strasse"&&phoneSongName("\xC5\x92uvre.wav",0)=="OEuvre",
              "a joined letter was not spelt out");
        check(phoneSongName("\xD7\xA9\xD7\x99\xD7\xA8.mp3",2)=="TRACK 3"&&phoneSongName("\xE2\x99\xAB.mp3",0)=="TRACK 1",
              "a name of nothing drawable is not TRACK and its number");
        // Each screen draws what it has to show: the same screen showing something else draws otherwise.
        PhoneView view;
        view.open=true;
        view.age=1;
        const auto drawn=[&](PhoneScreen s,auto&& change){
            PhoneView v=view;
            v.screen=s;
            change(v);
            std::vector<Vertex> out;
            appendPhone(out,v);
            check(out.size()%3==0,"a screen not in triangles");
            return out.size();
        };
        const auto nothing=[](PhoneView&){};
        const auto songs=[](PhoneView& v){
            v.tracks=2;
            std::snprintf(v.song,sizeof v.song,"%s","HELLO");
        };
        const auto longer=[&](PhoneView& v){
            songs(v);
            std::snprintf(v.song,sizeof v.song,"%s","HELLO AGAIN");
        };
        check(drawn(PhoneScreen::home,songs)!=drawn(PhoneScreen::home,longer),"the home screen did not show the song");
        check(drawn(PhoneScreen::music,nothing)!=drawn(PhoneScreen::music,songs),"MUSIC showed no songs");
        check(drawn(PhoneScreen::music,songs)!=drawn(PhoneScreen::music,longer),"MUSIC did not show the song");
        check(drawn(PhoneScreen::music,songs)!=drawn(PhoneScreen::music,[&](PhoneView& v){songs(v);v.failed=true;}),
              "MUSIC did not say a song could not play");
        check(drawn(PhoneScreen::gear,nothing)!=drawn(PhoneScreen::gear,[](PhoneView& v){
                  v.gearCount=1;
                  std::snprintf(v.gear[0].name,sizeof v.gear[0].name,"%s","WEB BOMB");
                  v.gear[0].most=4;
              }),
              "GEAR showed no gadgets");
        // MAP shows the same whatever else the phone has: more than a phone with no screen at all.
        check(drawn(PhoneScreen::map,nothing)>drawn(static_cast<PhoneScreen>(phoneApps+1),nothing),"MAP showed nothing");
    });
    test("sky anchors respect body occlusion and later world obstruction", [] {
        struct Occluded : TestWorld {
            mutable unsigned queries{};
            std::optional<RayHit> raycast(Vec3 o,Vec3 d,float distance) const override {
                if(queries++==0)return {}; // clear hand ray
                return RayHit{o+d*std::min(5.f,distance),{},1,true};
            }
        } blocked;
        Swing s(inert());auto in=aimed();
        s.update(.01f,in,blocked);
        check(!s.webs()[0].attached,"sky fallback bypassed a wall beside the hand");
        TestWorld w;w.enabled=false;Swing clear(inert());
        Body body{{0,1,0},{},true};clear.predictNativeStep(.01f,in,w,body);
        check(clear.webs()[0].attached,"clear sky refused attachment");
        w.enabled=true;w.anchor={0,10,0};
        for(int i=0;i<20;++i)clear.predictNativeStep(.01f,in,w,body);
        check(!clear.webs()[0].attached,"sky rope crossed newly streamed geometry");
    });
    test("moving surfaces rejected until adapter supports them", [] {
        TestWorld w;
        w.moving = true;
        Swing s;
        s.update(.01f, aimed(), w);
        check(!s.webs()[0].attached, "moving anchor accepted");
    });
    test("maximum range enforced", [] {
        TestWorld w;
        w.anchor.y = 200;
        Swing s;
        s.update(.01f, aimed(), w);
        check(s.webs()[0].attached&&s.webs()[0].airAnchor, "out-of-range geometry disabled sky anchor");
        near(s.webs()[0].anchor.y,100);
    });
    test("release keeps tangential momentum", [] {
        TestWorld w;
        Swing s(inert());
        s.reset({{0, 0, 0}, {8, 0, 0}, false});
        auto in = aimed();
        s.update(.01f, in, w);
        const auto v = s.body().velocity;
        in.hands[0].grip = 0;
        s.update(.01f, in, w);
        check(!s.webs()[0].attached, "still attached");
        near(length(s.body().velocity - v), 0);
    });
    test("slack web never pushes body outward", [] {
        TestWorld w;
        Swing s(inert());
        s.reset({{0, 0, 0}, {0, 5, 0}, false});
        auto in = aimed();
        for (int i = 0; i < 60; ++i)
            s.update(1.f / 120, in, w);
        near(s.body().velocity.y, 5);
        check(s.body().position.y > 2, "did not move inward");
    });
    test("pendulum remains within rope length", [] {
        TestWorld w;
        Swing s;
        s.reset({{0, 0, 0}, {15, 0, 0}, false});
        auto in = aimed();
        for (int i = 0; i < 2400; ++i) {
            s.update(1.f / 120, in, w);
            check(length(s.body().position - w.anchor) <= 20.002f, "rope stretched");
            check(finite(s.body().velocity), "nonfinite");
        }
    });
    test("passive pendulum does not gain energy", [] {
        TestWorld w;
        Swing s;
        s.reset({{0, 0, 0}, {15, 0, 0}, false});
        auto in = aimed();
        float peak = 0;
        for (int i = 0; i < 1200; ++i) {
            s.update(1.f / 120, in, w);
            float e = .5f * dot(s.body().velocity, s.body().velocity) + 9.81f * s.body().position.y;
            peak = std::max(peak, e);
        }
        check(peak < 113, "energy injection");
    });
    test("in the air a taut web takes in its slack: a body thrown at its end hangs where it got to", [] {
        // Thrown straight up at the web's end, 20 m over it, at 10 m/s: it gets
        // 5.1 m up. A web that kept its length let it fall back all the way.
        const auto rise=[](bool taut){
            TestWorld w;auto c=inert();c.gravity=9.81f;c.taut=taut;Swing s(c);
            auto in=aimed();in.hands[0].trigger=0;
            Body body{{0,0,0},{0,10,0},false};
            float lowest=100;
            for(int i=0;i<180;++i){
                const auto intent=s.predictNativeStep(1.f/60,in,w,body);
                check(intent.valid,"prediction rejected");
                body={intent.target,intent.velocity,false};
                if(i>=70)lowest=std::min(lowest,body.position.y);
            }
            check(s.webs()[0].attached,"the web let go");
            return std::pair{lowest,s.webs()[0].length};
        };
        const auto [held,rope]=rise(true);
        check(held>4.8f&&held<5.3f,"the taut web let the body fall from where it got to");
        near(rope,20-held,.05f);
        check(rise(false).first<.1f,"the web that kept its length held the body up");
    });
    test("a web in the air swings the body along the ground, not into it", [] {
        // A street at y = 0, the web's end 20 m over it and 25 m on from where
        // the body is shot from at rest, 15 m up: the swing's bottom, on a
        // 25.5 m rope, is 5.5 m under the street. The body skims it instead,
        // its fall turned along it, and swings on.
        const auto swing=[](float clearance){
            Street w;auto c=inert();c.gravity=9.81f;c.taut=true;c.groundClearance=clearance;Swing s(c);
            const Vec3 from{-25,15,0};
            const auto in=aimedAt(from,w.anchor);
            Body body{from,{},false};
            float lowest=100,furthest=-100;
            for(int i=0;i<300;++i){
                const auto intent=s.predictNativeStep(1.f/60,in,w,body);
                check(intent.valid,"prediction rejected");
                body={intent.target,intent.velocity,false};
                lowest=std::min(lowest,body.position.y);furthest=std::max(furthest,body.position.x);
            }
            check(s.webs()[0].attached,"the web let go");
            return std::pair{lowest,furthest};
        };
        const auto [kept,across]=swing(2.5f);
        check(kept>2.3f,"the swing came nearer the street than its clearance");
        check(across>3,"held off the street, the body did not swing on past the web's end");
        check(swing(0).first<-4,"without a clearance the swing did not go under the street");
        auto bad=inert();bad.groundClearance=2;bad.groundTurn=0;
        bool threw=false;
        try{Swing broken(bad);(void)broken;}catch(const std::invalid_argument&){threw=true;}
        check(threw,"a clearance with no brake accepted");
    });
    test("fixed physics agrees across refresh rates", [] {
        auto run = [](int hz) {
            TestWorld w;
            Swing s;
            s.reset({{0, 0, 0}, {10, 0, 0}, false});
            auto in = aimed();
            for (int i = 0; i < hz * 4; ++i)
                s.update(1.f / hz, in, w);
            return s.body().position;
        };
        const auto baseline = run(120);
        for (int hz : {45, 72, 90, 144})
            check(length(run(hz) - baseline) < .12f, "refresh dependent trajectory");
    });
    test("independent hands can hold and release", [] {
        TestWorld w;
        Swing s;
        auto in = aimed();
        in.hands[1] = in.hands[0];
        s.update(.02f, in, w);
        check(s.webs()[0].attached && s.webs()[1].attached, "dual attach");
        in.hands[0].grip = 0;
        s.update(.02f, in, w);
        check(!s.webs()[0].attached && s.webs()[1].attached, "cross-hand release");
    });
    test("tracking loss releases and requires grip reset", [] {
        TestWorld w;
        Swing s;
        auto in = aimed();
        s.update(.01f, in, w);
        in.hands[0].tracked = false;
        s.update(.01f, in, w);
        check(!s.webs()[0].attached, "tracking stuck");
        in.hands[0].tracked = true;
        s.update(.01f, in, w);
        check(!s.webs()[0].attached, "reattached without release");
        in.hands[0].grip = 0;
        s.update(.01f, in, w);
        in.hands[0].grip = 1;
        s.update(.01f, in, w);
        check(s.webs()[0].attached, "cannot rearm");
    });
    test("focus loss releases and freezes physics", [] {
        TestWorld w;
        Swing s;
        auto in = aimed();
        s.update(.01f, in, w);
        auto p = s.body().position;
        in.focused = false;
        s.update(.09f, in, w);
        check(!s.webs()[0].attached, "focus stuck");
        near(length(s.body().position - p), 0);
    });
    test("long stall releases without catch-up teleport", [] {
        TestWorld w;
        Swing s;
        auto in = aimed();
        s.update(.01f, in, w);
        auto p = s.body().position;
        s.update(3, in, w);
        near(length(s.body().position - p), 0);
        check(!s.webs()[0].attached, "stall stuck");
    });
    test("unloaded surface detaches", [] {
        TestWorld w;
        Swing s;
        auto in = aimed();
        s.update(.01f, in, w);
        w.enabled = false;
        s.update(.01f, in, w);
        check(!s.webs()[0].attached, "invalid anchor");
    });
    test("fast short hand twitch does not zip", [] {
        TestWorld w;
        Swing s(inert());
        auto in = aimed();
        s.update(.01f, in, w);
        in.hands[0].gripRelativeToHead.y = -.02f;
        s.update(.01f, in, w);
        near(length(s.body().velocity), 0);
    });
    test("physical yank creates one bounded zip", [] {
        TestWorld w;
        Swing s(inert());
        auto in = aimed();
        s.update(.01f, in, w);
        int zips = 0;
        for (int i = 0; i < 20; ++i) {
            in.hands[0].gripRelativeToHead.y -= .03f;
            s.update(.01f, in, w);
            for (auto e : s.events())
                zips += e.kind == EventKind::Zip;
        }
        check(zips == 1, "zip must fire once per attachment");
        check(s.body().velocity.y > 0 && s.body().velocity.y <= 12, "zip strength");
    });
    test("slow hand movement does not zip", [] {
        TestWorld w;
        Swing s(inert());
        auto in = aimed();
        for (int i = 0; i < 200; ++i) {
            in.hands[0].gripRelativeToHead.y -= .001f;
            s.update(.01f, in, w);
        }
        near(length(s.body().velocity), 0);
    });
    test("virtual travel cannot trigger yank", [] {
        TestWorld w;
        Swing s(inert());
        s.reset({{0, 0, 0}, {25, 0, 0}, false});
        auto in = aimed();
        for (int i = 0; i < 200; ++i) {
            s.update(.01f, in, w);
            for (auto e : s.events())
                check(e.kind != EventKind::Zip, "travel triggered zip");
        }
    });
    test("reel requires trigger release after attachment", [] {
        TestWorld w;
        Swing s(inert());
        auto in = aimed();
        s.update(.02f, in, w);
        const float initial = s.webs()[0].length;
        for (int i = 0; i < 20; ++i)
            s.update(.01f, in, w);
        near(s.webs()[0].length, initial);
        in.hands[0].trigger = 0;
        s.update(.01f, in, w);
        in.hands[0].trigger = 1;
        for (int i = 0; i < 20; ++i)
            s.update(.01f, in, w);
        check(s.webs()[0].length < initial - .5f, "reel did not shorten");
    });
    test("native reel engages slack immediately without committing a teleport", [] {
        TestWorld world;
        auto config = game_swing::physicsConfig({});
        config.gravity = 0;
        // Slack in the air, as webs had before they were taut there.
        config.taut = false;
        Swing swing(config);
        Body actual{{0, 0, 0}, {}, false};
        auto in = aimed();
        swing.predictNativeStep(.02f, in, world, actual);
        // To the hand that holds it, 0.6 m above the body's centre (physical webs).
        near(swing.webs()[0].length, 19.4f);
        // Native movement/yank carried us ten metres toward the existing anchor.
        actual.position.y = 10;
        in.hands[0].trigger = 0;
        swing.predictNativeStep(.02f, in, world, actual);
        near(swing.webs()[0].length, 19.4f);
        in.hands[0].trigger = 1;
        const auto pull = swing.predictNativeStep(.02f, in, world, actual);
        check(pull.valid && pull.target.y > 10.1f && pull.target.y < 10.5f,
              "first reel frame spent time winding slack or teleported");
        near(length(swing.body().position - actual.position), 0);
        near(length(swing.webs()[0].anchor - world.anchor), 0);
        actual.velocity = (pull.target - actual.position) / .02f;
        actual.position = pull.target;
        in.hands[0].trigger = in.hands[0].grip = 0;
        const auto released = swing.predictNativeStep(.02f, in, world, actual);
        check(!swing.webs()[0].attached, "grip release kept the web");
        near(length(released.velocity - actual.velocity), 0);
        check(released.target.y > actual.position.y, "release lost reel momentum");
        // Taut, as in the game, the web takes those ten metres in at once.
        auto tautConfig = game_swing::physicsConfig({});
        tautConfig.gravity = 0;
        Swing taut(tautConfig);
        Body body{{0, 0, 0}, {}, false};
        auto hold = aimed();
        hold.hands[0].trigger = 0;
        taut.predictNativeStep(.02f, hold, world, body);
        body.position.y = 10;
        taut.predictNativeStep(.02f, hold, world, body);
        near(taut.webs()[0].length, 9.4f);
    });
    test("native swing profile retains speed and steering across physics rates", [] {
        const auto config = game_swing::physicsConfig({});
        for (float hz : {45.f, 72.f, 90.f, 120.f}) {
            TestWorld world;
            Swing swing(config);
            Body actual{{0, 0, 0}, {20, 0, 0}, false};
            auto in = aimed();
            const float dt = 1.f / hz;
            // Each step continues from the solver's end velocity, as the
            // native adapter's in-flight prediction does.
            for (unsigned i = 0; i < static_cast<unsigned>(hz); ++i) {
                const auto result = swing.predictNativeStep(dt, in, world, actual);
                check(result.valid && swing.webs()[0].attached, "native swing lost its web");
                actual.position = result.target;
                actual.velocity = result.velocity;
                check(length(actual.velocity) <= config.maxSpeed + .1f, "speed limit exceeded");
                check(length(actual.position + Vec3{0, .6f, 0} - world.anchor) <= swing.webs()[0].length + .01f,
                      "native swing escaped the rope");
            }
            check(length(actual.velocity) > 12, "native swing still feels capped at walking speed");
            // A clear lateral input should change flight within a quarter second.
            in.hands[0].grip = 0;
            in.move = {0, 0, 1};
            for (unsigned i = 0; i < static_cast<unsigned>(hz / 4); ++i) {
                const auto result = swing.predictNativeStep(dt, in, world, actual);
                actual.velocity = result.velocity;
                actual.position = result.target;
            }
            check(actual.velocity.z > .9f, "air steering did not respond promptly");
        }
    });
    // MoverStandard as the swing adapter sees it: each physics step applies the
    // newest command at its start, and the adapter observes that step's start
    // position and the previous step's velocity, then submits the next command.
    struct NativeMover {
        float dt = 1.f / 42, wall = 1e9f; // the measured October 5 step; optional wall at x
        Vec3 position{}, last{}, command{};
        uint64_t step{}, serial{};
        bool controlled{}, settle = true;
        float previousDt = 1.f / 42; // length of the step that produced `position`
        game_swing::InFlightStep flight;
        // One step: observe, steer, then let the native step move the body.
        // `dt` is the length of the step starting now; the game varies it.
        Vec3 run(Swing& swing, const Input& in, const WorldQueries& world) {
            ++step;
            const Vec3 achieved = (position - last) / previousDt;
            const auto body = flight.predict({step, serial, position, achieved, dt, controlled}, false);
            if (settle && step > 1)
                swing.settleStep(previousDt, dt); // the last prediction assumed previousDt
            const auto intent = swing.predictNativeStep(dt, in, world, body, dt);
            check(intent.valid, "native prediction rejected");
            const Vec3 average = (intent.target - body.position) / dt;
            // The adapter's cap (game_swing.cpp), and the movement module's
            // step: the command in flight, as far as the module takes it.
            const Vec3 requested = limited(
                average, std::min(swing.config().maxSpeed, .875f * native_movement::farthestStep / dt));
            const Vec3 travel = controlled ? native_movement::stepDelta(command, dt) : achieved * dt;
            flight.issued(step + 1000, requested,
                          limited(intent.velocity + requested - average, swing.config().maxSpeed));
            serial = step + 1000;
            command = requested;
            controlled = true;
            last = position;
            position += travel;
            position.x = std::min(position.x, wall);
            previousDt = dt;
            return achieved;
        }
    };
    test("native flight steers from the step in flight, not the observed one", [] {
        // Steering from the observation alone made even and odd physics steps
        // two trajectories with a third of the configured gravity each. A yank
        // kicked only one of them, and the October 5 view shook until landing.
        const auto config = game_swing::physicsConfig({});
        TestWorld world;
        Swing swing(config);
        NativeMover mover;
        mover.position = {0, 0, 0};
        mover.last = mover.position - Vec3{8, 0, 0} * mover.dt;
        auto in = aimed();
        std::vector<Vec3> seen;
        for (unsigned i = 0; i < 40; ++i) {
            if (i >= 10 && i < 14)
                in.hands[0].gripRelativeToHead.y -= .06f; // yank down, away from the anchor above
            if (i == 20)
                in.hands[0].grip = 0;
            seen.push_back(mover.run(swing, in, world));
        }
        check(!swing.webs()[0].attached, "web not released");
        // A command issued in run k moves the body in step k+1 and is observed
        // in run k+2. The yank's command reaches the observations by run 15.
        check(seen[15].y > seen[9].y + 3, "yank did not reach the native step");
        // Free flight: consecutive steps differ by gravity only, and the
        // velocity falls at the configured rate.
        for (size_t i = 23; i < seen.size(); ++i)
            check(length(seen[i] - seen[i - 1] - Vec3{0, -config.gravity * mover.dt, 0}) < .02f,
                  "alternating velocities after a yank");
        const float fall = (seen[22].y - seen.back().y) / (mover.dt * static_cast<float>(seen.size() - 1 - 22));
        near(fall, config.gravity, .05f);
    });
    test("letting go of a web while reeling keeps the winch speed", [] {
        // The body is carried in at the reel rate. With that motion applied to
        // position only, the October 5 probe lost 16 m/s in one step at release.
        TestWorld w;
        Swing s(inert());
        s.reset({{0, 0, 0}, {}, false});
        auto in = aimed();
        in.hands[0].trigger = 0;
        s.update(.01f, in, w);
        check(s.webs()[0].attached, "no web");
        in.hands[0].trigger = 1;
        for (int i = 0; i < 50; ++i)
            s.update(.01f, in, w);
        near(s.body().velocity.y, s.config().reelSpeed, .05f);
        const float height = s.body().position.y;
        in.hands[0].grip = in.hands[0].trigger = 0;
        for (int i = 0; i < 25; ++i)
            s.update(.01f, in, w);
        check(!s.webs()[0].attached, "web not released");
        near(s.body().velocity.y, s.config().reelSpeed, .05f);
        near(s.body().position.y - height, s.config().reelSpeed * .25f, .1f);
        // Stopping the reel while holding on leaves a slack rope: the body
        // coasts inward, and the rope neither pushes nor lengthens.
        Swing held(inert());
        held.reset({{0, 0, 0}, {}, false});
        in = aimed();
        in.hands[0].trigger = 0;
        held.update(.01f, in, w);
        in.hands[0].trigger = 1;
        for (int i = 0; i < 50; ++i)
            held.update(.01f, in, w);
        const float reeled = held.webs()[0].length;
        in.hands[0].trigger = 0;
        for (int i = 0; i < 25; ++i)
            held.update(.01f, in, w);
        check(held.webs()[0].attached, "web lost when the reel stopped");
        near(held.webs()[0].length, reeled);
        near(held.body().velocity.y, held.config().reelSpeed, .05f);
    });
    test("two reeling webs carry the body together and it keeps that speed on release", [] {
        // Each rope shortens at the reel speed; at an angle they move the body
        // faster than that. The velocity has to hold it, or letting go jolts.
        TwoAnchors w;
        Swing s(inert());
        s.reset({{0, 0, 0}, {}, false});
        auto in = w.aimed({0, 0, 0});
        s.update(.01f, in, w);
        check(s.webs()[0].attached && s.webs()[1].attached, "webs not attached");
        in.hands[0].trigger = in.hands[1].trigger = 1;
        for (int i = 0; i < 50; ++i)
            s.update(.01f, in, w);
        const Vec3 up = s.body().position;
        const float cosine = (w.anchors[0].y - up.y) / length(w.anchors[0] - up);
        near(up.x, 0, .01f);
        check(up.y > 4, "the winches did not lift the body");
        near(s.body().velocity.y, s.config().reelSpeed / cosine, .2f);
        const float speed = s.body().velocity.y;
        for (auto& hand : in.hands)
            hand.grip = hand.trigger = 0;
        for (int i = 0; i < 25; ++i)
            s.update(.01f, in, w);
        check(!s.webs()[0].attached && !s.webs()[1].attached, "webs not released");
        near(s.body().velocity.y, speed, .01f);
        near(s.body().position.y - up.y, speed * .25f, .1f);
    });
    test("winches on two ropes never run away with the body", [] {
        // The exact carried speed grows without bound as two taut ropes come to
        // oppose each other. The velocity follows it up to a right angle only.
        for (float start : {0.f, 6.f, 12.f, 16.f, 19.f, 19.9f})
            for (int mode = 0; mode < 3; ++mode) {
                TwoAnchors w;
                Swing s(inert());
                s.reset({{0, start, 0}, {}, false});
                auto in = w.aimed({0, start, 0});
                s.update(.01f, in, w);
                check(s.webs()[0].attached && s.webs()[1].attached, "webs not attached");
                in.hands[0].trigger = mode != 1;
                in.hands[1].trigger = mode != 0;
                for (int i = 0; i < 300; ++i) {
                    const Vec3 previous = s.body().position;
                    s.update(.01f, in, w);
                    check(length(s.body().velocity) <= s.config().reelSpeed * 1.5f,
                          "winches ran away with the body");
                    check(length(s.body().position - previous) < .65f, "winches teleported the body");
                    for (const auto& web : s.webs())
                        check(web.attached && length(s.body().position - web.anchor) <= web.length + .04f,
                              "winched rope exceeded");
                }
            }
    });
    test("a rope at its shortest length ignores frame time corrections", [] {
        TestWorld w;
        w.anchor = {0, 4, 0};
        Swing s(inert());
        s.reset({{0, 0, 0}, {}, false});
        auto in = aimed();
        in.hands[0].trigger = 0;
        s.update(.01f, in, w);
        check(s.webs()[0].attached, "no web");
        in.hands[0].trigger = 1;
        s.update(.01f, in, w);
        const float reeling = s.webs()[0].length;
        check(reeling < 4, "the winch did not start");
        s.settleStep(.02f, .03f); // the step ran 10 ms longer than predicted
        near(s.webs()[0].length, reeling - s.config().reelSpeed * .01f);
        s.settleStep(.03f, .02f);
        near(s.webs()[0].length, reeling);
        s.settleStep(.02f, 1.f); // not a frame time
        s.settleStep(.02f, std::numeric_limits<float>::quiet_NaN());
        near(s.webs()[0].length, reeling);
        // A 200 ms step was predicted as the longest one, half of it: the
        // body went half the way in the 50 ms more, and the rope winds for half.
        s.settleStep(2 * longestNativeStep, 2 * longestNativeStep + .05f);
        near(s.webs()[0].length, reeling - s.config().reelSpeed * .025f);
        s.settleStep(2 * longestNativeStep, 2 * longestNativeStep - .05f);
        near(s.webs()[0].length, reeling);
        for (int i = 0; i < 100; ++i)
            s.update(.01f, in, w);
        near(s.webs()[0].length, s.config().minRope);
        // The winch took nothing in at the limit, so there is nothing to correct.
        s.settleStep(.03f, .02f);
        near(s.webs()[0].length, s.config().minRope);
        s.settleStep(.02f, .03f);
        near(s.webs()[0].length, s.config().minRope);
    });
    test("native reeling stays smooth when the frame time varies and keeps its speed on release", [] {
        // Frame times in the October 5 probe moved between 15 and 33 ms. The
        // rope reeled for the predicted time while the body travelled for the
        // real one, so the rope snapped the body by centimetres each time.
        const float frames[] = {.018f, .026f, .0282f, .026f, .0178f, .0188f, .0196f, .0188f, .024f, .016f};
        for (bool settle : {false, true}) {
            const auto config = game_swing::physicsConfig({});
            TestWorld world;
            world.anchor = {0, 80, 0};
            Swing swing(config);
            NativeMover mover;
            mover.settle = settle;
            mover.last = mover.position;
            auto in = aimed();
            in.hands[0].trigger = 0;
            float roughest = 0, before = 0, after = 0;
            Vec3 previous{};
            for (unsigned i = 0; i < 90; ++i) {
                mover.dt = frames[i % 10];
                if (i == 3)
                    in.hands[0].trigger = 1; // reel
                if (i == 70)
                    in.hands[0].grip = in.hands[0].trigger = 0;
                const Vec3 observed = mover.run(swing, in, world);
                if (i >= 12 && i < 70)
                    roughest = std::max(roughest, length(observed - previous));
                if (i == 70)
                    before = length(observed);
                if (i == 75)
                    after = length(observed);
                previous = observed;
            }
            check(before > config.reelSpeed - 1, "reel never reached its speed");
            if (settle) {
                // Gravity alone changes the speed by 6 m/s^2 x 28 ms = 0.17 m/s per step.
                check(roughest < .25f, "frame time changes still jolt a reeling body");
                check(after > before - 1.f, "release lost the reel speed");
            } else {
                check(roughest > .5f, "the unsettled comparison no longer shows the jolt");
            }
        }
    });
    test("the game's webs swing a native body over the street, and catch a fall past their end without a jolt", [] {
        // The game's swing (physicsConfig) at real weight, its mover stepping
        // 16 to 28 ms, a web shot at rest to its end 20 m over the street.
        const float frames[] = {.018f, .026f, .0282f, .026f, .0178f, .0188f, .0196f, .0188f, .024f, .016f};
        struct Flight {
            float lowest = 100, roughest{}, furthest = -100;
            bool attached{};
        };
        const auto fly = [&](Vec3 from, bool before) {
            auto config = game_swing::physicsConfig({});
            config.gravity = 9.81f;
            // The webs before October 15: slack in the air, and no clearance.
            if (before) {
                config.taut = false;
                config.groundClearance = 0;
            }
            Street world;
            Swing swing(config);
            NativeMover mover;
            mover.position = mover.last = from;
            const auto in = aimedAt(from, world.anchor);
            Flight f;
            Vec3 previous{};
            for (unsigned i = 0; i < 200; ++i) {
                mover.dt = frames[i % 10];
                const Vec3 observed = mover.run(swing, in, world);
                if (i >= 2)
                    f.roughest = std::max(f.roughest, length(observed - previous));
                previous = observed;
                f.lowest = std::min(f.lowest, mover.position.y);
                f.furthest = std::max(f.furthest, mover.position.x);
            }
            f.attached = swing.webs()[0].attached;
            return f;
        };
        // From 15 m up and 25 m short of the web's end: its swing, 25.4 m
        // long, would bottom out 6 m under the street.
        const auto low = fly({-25, 15, 0}, false);
        check(low.attached, "the web let go");
        check(low.lowest > 1.2f, "the native swing came nearer the street than its clearance");
        check(low.furthest > 3, "the native swing did not go on past the web's end");
        check(low.roughest < 2, "winding the web in jolted the native swing");
        // From 10 m over the web's end: the body falls past it. The old web
        // went slack and caught it 10 m/s at once.
        const auto over = fly({-10, 30, 0}, false), old = fly({-10, 30, 0}, true);
        check(old.roughest > 8, "the old web no longer shows its catch");
        check(over.attached && over.roughest < 2, "a taut web jolted the body falling past its end");
        check(over.lowest > 1.2f, "the fall past the web's end came too near the street");
    });
    test("native flight removes motion a wall blocked exactly once", [] {
        TestWorld world;
        world.enabled = false;
        Swing swing(game_swing::physicsConfig({}));
        NativeMover mover;
        mover.wall = 1;
        mover.position = {0, 50, 0};
        mover.last = mover.position - Vec3{10, 0, 0} * mover.dt;
        Input in;
        float lowest = 1e9f;
        for (unsigned i = 0; i < 30; ++i) {
            const auto observed = mover.run(swing, in, world);
            lowest = std::min(lowest, mover.command.x);
            check(observed.x > -.01f, "wall contact reversed the body");
        }
        check(lowest > -.01f && mover.command.x < .01f, "solver kept pushing into or away from the wall");
        check(mover.command.y < -2, "sliding down the wall stopped falling");
        near(mover.position.x, 1);
    });
    test("a swing goes on through physics steps longer than 50 ms", [] {
        // The game's step is its smoothed frame time. In the October 10
        // headset report it crossed 50 ms three times in mid-swing: the
        // movement module ran those steps without the command, the prediction
        // was refused and the webs let go, and the game's own fall took the
        // player down at 42 m/s. These are the steps of that report.
        const float frames[] = {.0444f, .0458f, .0473f, .0495f, .0509f, .0514f, .0515f, .0508f, .0562f,
                                .0542f, .0529f, .0538f, .0557f, .0556f, .0582f, .0626f, .0658f, .0665f,
                                .0665f, .0655f, .0612f, .0533f, .0481f, .0469f, .0450f, .0412f};
        const auto config = game_swing::physicsConfig({});
        TestWorld world;
        world.anchor = {0, 60, 0};
        Swing swing(config);
        NativeMover mover;
        mover.dt = mover.previousDt = .0444f;
        mover.last = mover.position - Vec3{20, 0, 0} * mover.dt;
        const auto in = aimed();
        float roughest = 0, lowest = 0;
        Vec3 previous{20, 0, 0};
        for (float dt : frames) {
            mover.dt = dt;
            const Vec3 observed = mover.run(swing, in, world); // checks that the prediction is valid
            check(swing.webs()[0].attached, "a long step let go of the web");
            check(length(mover.position + Vec3{0, .6f, 0} - world.anchor) < swing.webs()[0].length + .05f,
                  "the body left its rope in a long step");
            roughest = std::max(roughest, length(observed - previous));
            lowest = std::min(lowest, observed.y);
            previous = observed;
        }
        // The rope turns 20 m/s on 60 m and gravity 6 m/s^2 by under 1 m/s a step.
        check(roughest < 1, "a long step jolted the swing");
        check(lowest > -.5f, "the swing dived");
        check(length(previous) > 15 && mover.position.x > 20, "the swing lost its speed in the long steps");
    });
    test("a step the movement module shortens takes no speed from the flight", [] {
        // At the speed limit a command asks for 1.75 m of the step observed.
        // The step it governs can be a quarter longer (the October 10 report
        // has 16.0 then 20.1 ms): the module moves the body 2 m in it, never
        // more. That is no wall the body met.
        game_swing::InFlightStep flight;
        flight.predict({1, 0, {0, 0, 0}, {40, 0, 0}, .04f, false}, false);
        flight.issued(5, {40, 0, 0}, {40, 0, 0});
        auto body = flight.predict({2, 5, {1.6f, 0, 0}, {40, 0, 0}, .0665f, true}, false);
        near(body.position.x, 1.6f + native_movement::farthestStep);
        near(body.velocity.x, 40);
        flight.issued(6, {26, 0, 0}, {39, -1, 0});
        body = flight.predict({3, 6, {3.6f, 0, 0}, {2 / .0665f, 0, 0}, .0665f, true}, false);
        near(body.velocity.x, 39);
        near(body.velocity.y, -1);
        near(body.position.x, 3.6f + 26 * .0665f);
        // A wall still takes what it blocked: here all of the same command.
        game_swing::InFlightStep blocked;
        blocked.predict({1, 0, {0, 0, 0}, {40, 0, 0}, .04f, false}, false);
        blocked.issued(5, {40, 0, 0}, {40, 0, 0});
        blocked.predict({2, 5, {1.6f, 0, 0}, {40, 0, 0}, .0665f, true}, false);
        blocked.issued(6, {26, 0, 0}, {39, -1, 0});
        body = blocked.predict({3, 6, {1.6f, 0, 0}, {0, 0, 0}, .0665f, true}, false);
        near(body.velocity.x, 0);
        near(body.velocity.y, -1);
        // In flight: free fall at 60 m/s with the speed limit at 65, the
        // step a quarter longer from one frame to the next.
        auto config = game_swing::physicsConfig({});
        config.maxSpeed = 65;
        TestWorld world;
        world.enabled = false;
        Swing swing(config);
        NativeMover mover;
        mover.dt = mover.previousDt = .028f;
        mover.position = {0, 500, 0};
        mover.last = mover.position - Vec3{60, 0, 0} * mover.dt;
        Input none;
        Vec3 observed{};
        for (unsigned i = 0; i < 12; ++i) {
            if (i == 6)
                mover.dt = .035f;
            observed = mover.run(swing, none, world);
            check(observed.x > 49, "a shortened step stopped the flight");
        }
        // The longer step's own cap is 1.75 m a step: 50 m/s.
        near(observed.x, 1.75f / .035f, .5f);
    });
    test("swept collision stops tunneling and slides", [] {
        LabWorld w({{{-1, -10, -10}, {1, 10, 10}, {}, 1}});
        auto hit = w.sweep({-20, 0, 0}, {20, 0, 5}, .35f);
        check(hit.hit, "missed wall");
        check(hit.position.x < -1.34f, "tunneled");
        check(hit.position.z > 4.9f, "no slide");
    });
    test("floor catches falling body", [] {
        LabWorld w({{{-100, -1, -100}, {100, 0, 100}, {}, 1}});
        Swing s;
        s.reset({{0, 10, 0}, {0, -60, 0}, false});
        Input in;
        for (int i = 0; i < 180; ++i)
            s.update(1.f / 90, in, w);
        near(s.body().position.y, .3501f, .005f);
        check(s.body().grounded, "not grounded");
    });
    test("head translation and IPD preserved", [] {
        Rig rig{{10, 20, 30}, .5f};
        Pose left{{-.032f, 1.7f, 0}, {}}, right{{.032f, 1.7f, 0}, {}};
        near(length(rig.toWorld(left).position - rig.toWorld(right).position), .064f);
        left.position.y += .2f;
        near(rig.toWorld(left).position.y, 21.9f);
    });
    test("snap turn rotates around head without orbit", [] {
        Rig rig{{0, 0, 0}, 0};
        Vec3 h{.3f, 1.7f, .5f};
        const auto before = rig.toWorld({h, {}}).position;
        rig.turn(.5235988f, h);
        near(length(rig.toWorld({h, {}}).position - before), 0);
    });
    test("runtime recenter preserves head position and heading", [] {
        Rig rig{{2, 3, 4}, .7f};
        Pose oldTracked{{.2f, 1.7f, -.1f}, Quat::yaw(.3f)};
        const auto worldHead = rig.toWorld(oldTracked);
        Pose newTracked{{0, 1.7f, 0}, Quat::yaw(-.2f)};
        rig.preserveHead(worldHead, newTracked);
        auto actual = rig.toWorld(newTracked);
        near(length(actual.position - worldHead.position), 0);
        near(length(actual.orientation.rotate({0, 0, -1}) - worldHead.orientation.rotate({0, 0, -1})), 0);
    });
    test("two distinct anchors remain bounded", [] {
        LabWorld w({{{-12, 15, -1}, {-10, 25, 1}, {}, 1}, {{10, 15, -1}, {12, 25, 1}, {}, 2}});
        Swing s;
        s.reset({{0, 0, 0}, {0, 0, 6}, false});
        auto in = aimed();
        const float angle = .4636476f;
        in.hands[0].aim.orientation =
            Quat{0, 0, std::sin(angle / 2), std::cos(angle / 2)} * in.hands[0].aim.orientation;
        in.hands[1] = in.hands[0];
        in.hands[1].aim.orientation =
            Quat{0, 0, -std::sin(angle / 2), std::cos(angle / 2)} * Quat{.70710678f, 0, 0, .70710678f};
        s.update(.01f, in, w);
        check(s.webs()[0].attached && s.webs()[1].attached, "dual targets not attached");
        for (int i = 0; i < 300; ++i) {
            s.update(.01f, in, w);
            for (auto web : s.webs())
                check(length(s.body().position - web.anchor) <= web.length + .02f, "dual rope violated");
        }
        in.hands[0].trigger = in.hands[1].trigger = 0;
        s.update(.01f, in, w);
        in.hands[0].trigger = in.hands[1].trigger = 1;
        for (int i = 0; i < 900; ++i) {
            const auto previous = s.body().position;
            s.update(.01f, in, w);
            const auto& webs = s.webs();
            check(webs[0].attached && webs[1].attached, "dual winch detached");
            check(webs[0].length + webs[1].length >= length(webs[0].anchor - webs[1].anchor),
                  "winches requested impossible rope lengths");
            check(length(s.body().position - previous) < .65f, "opposing winches teleported player");
            for (auto web : webs)
                check(length(s.body().position - web.anchor) <= web.length + .04f,
                      "dual winch constraint exceeded");
        }
    });
    test("point launch requires zip then landing then jump", [] {
        struct FloorWorld : TestWorld {
            MoveResult sweep(Vec3, Vec3 to, float radius) const override {
                if (to.y < radius)
                    return {{to.x, radius, to.z}, {0, 1, 0}, true};
                return {to, {}, false};
            }
        } w;
        SwingConfig c;
        c.gravity = 20;
        c.zipMultiplier = 1;
        c.maxZipImpulse = 2;
        Swing s(c);
        s.reset({{0, .35f, 0}, {}, true});
        auto in = aimed();
        s.update(.01f, in, w);
        bool zipped = false;
        for (int i = 0; i < 7; ++i) {
            in.hands[0].gripRelativeToHead.y -= .03f;
            s.update(.01f, in, w);
            for (auto e : s.events())
                zipped |= e.kind == EventKind::Zip;
        }
        check(zipped, "no zip");
        in.hands[0].grip = 0;
        bool landed = false;
        for (int i = 0; i < 60; ++i) {
            s.update(.01f, in, w);
            if (s.body().grounded) {
                landed = true;
                break;
            }
        }
        check(landed, "did not land");
        in.jump = true;
        s.update(.02f, in, w);
        bool launched = false;
        for (auto e : s.events())
            launched |= e.kind == EventKind::PointLaunch;
        check(launched, "point launch missing");
        check(s.body().velocity.y > c.jumpSpeed, "point jump too weak");
    });
    test("view matrix puts eye at origin", [] {
        Pose p{{1, 2, 3}, Quat::yaw(.7f)};
        auto m = viewMatrix(p);
        near(m[0] * p.position.x + m[1] * p.position.y + m[2] * p.position.z + m[3], 0);
        near(m[4] * p.position.x + m[5] * p.position.y + m[6] * p.position.z + m[7], 0);
        near(m[8] * p.position.x + m[9] * p.position.y + m[10] * p.position.z + m[11], 0);
    });
    test("projection maps asymmetric frustum and D3D depth", [] {
        auto p = projection(-.7f, .9f, -.8f, .6f, .1f, 100);
        auto project = [&](Vec3 v, int row) {
            return (p[row * 4] * v.x + p[row * 4 + 1] * v.y + p[row * 4 + 2] * v.z + p[row * 4 + 3]) / (-v.z);
        };
        near(project({std::tan(-.7f), 0, -1}, 0), -1);
        near(project({std::tan(.9f), 0, -1}, 0), 1);
        near(project({0, 0, -.1f}, 2), 0);
        near(project({0, 0, -100}, 2), 1);
    });
    test("invalid configuration rejected", [] {
        SwingConfig c;
        c.fixedStep = 0;
        bool threw = false;
        try {
            Swing s(c);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "invalid step accepted");
    });
    test("NaN input delta ignored", [] {
        TestWorld w;
        Swing s;
        auto p = s.body().position;
        s.update(std::numeric_limits<float>::quiet_NaN(), aimed(), w);
        near(length(s.body().position - p), 0);
    });
    test("nonfinite grip releases existing web", [] {
        TestWorld w;
        Swing s;
        auto in = aimed();
        s.update(.01f, in, w);
        in.hands[0].grip = std::numeric_limits<float>::quiet_NaN();
        s.update(.01f, in, w);
        check(!s.webs()[0].attached, "invalid grip held web");
    });
    test("hand beyond wall cannot attach through body obstruction", [] {
        LabWorld w({{{-5, 8, -5}, {5, 9, 5}, {}, 1}, {{-5, 20, -5}, {5, 21, 5}, {}, 2}});
        Swing s;
        s.reset({{0, 0, 0}, {}, false});
        auto in = aimed();
        in.hands[0].aim.position.y = 10;
        s.update(.01f, in, w);
        check(!s.webs()[0].attached, "web shot through wall");
    });
    test("a shot preview says what a grip press would do, and the press does it", [] {
        TestWorld w;
        Swing s(inert());
        s.reset({{0, 0, 0}, {}, false});
        const auto in = aimed();
        const auto surface = s.shot(in.hands[0].aim, s.body().position, w);
        check(surface.web && !surface.web->airAnchor && surface.hit, "surface not previewed");
        near(length(surface.web->anchor - w.anchor), 0);
        near(surface.web->length, 20);
        s.update(1.f / 90, in, w);
        check(s.webs()[0].attached, "the press missed the previewed anchor");
        near(length(s.webs()[0].anchor - surface.web->anchor), 0);
        // Open air: an air anchor at maximum reach, no surface.
        w.enabled = false;
        const auto air = s.shot(in.hands[0].aim, {0, 0, 0}, w);
        check(air.web && air.web->airAnchor && !air.hit, "open air not previewed");
        near(length(air.web->anchor - in.hands[0].aim.position), 100);
        // A moving surface holds no web; the preview still names what it met.
        w.enabled = w.moving = true;
        const auto moving = s.shot(in.hands[0].aim, {0, 0, 0}, w);
        check(!moving.web && moving.hit && !moving.hit->fixed, "moving surface previewed as an anchor");
        w.moving = false;
        check(!s.shot(in.hands[0].aim, w.anchor - Vec3{0, 1, 0}, w).web, "anchor inside the shortest rope previewed");
        // A wall between the body and the anchor the hand sees.
        LabWorld walls({{{-5, 8, -5}, {5, 9, 5}, {}, 1}, {{-5, 20, -5}, {5, 21, 5}, {}, 2}});
        auto high = in.hands[0].aim;
        high.position.y = 10;
        const auto walled = Swing{}.shot(high, {0, 0, 0}, walls);
        check(!walled.web && walled.hit, "web previewed through the body's wall");
        auto broken = in.hands[0].aim;
        broken.position.x = std::numeric_limits<float>::quiet_NaN();
        const auto none = s.shot(broken, {0, 0, 0}, w);
        check(!none.web && !none.hit, "broken aim previewed");
    });
    test("native eye command rejects malformed or unrelated views", [] {
        native_eyes::Command c;c.enabled=1;c.serial=1;
        const Mat4 basis={1,0,0,0,0,-1,0,0,0,0,-1,0,100,50,-200,1};
        for(unsigned i=0;i<2;++i) {
            c.eyes[i].world=native_view::relativePose(basis,{{i? .032f:-.032f,0,0},{}});
            const float fov[]={-.8f,.8f,-.8f,.8f};std::copy(std::begin(fov),std::end(fov),c.eyes[i].fov);
        }
        check(native_eyes::valid(c),"valid tracked pair rejected");
        auto bad=c;bad.eyes[1].world[12]+=1;check(!native_eyes::valid(bad),"unrelated cameras accepted");
        bad=c;bad.eyes[0].world[0]=2;check(!native_eyes::valid(bad),"scaled pose accepted");
        bad=c;bad.eyes[0].fov[0]=std::numeric_limits<float>::quiet_NaN();check(!native_eyes::valid(bad),"NaN lens accepted");
        bad=c;bad.leaseMs=501;check(!native_eyes::valid(bad),"unbounded lease accepted");
        c.anchor={100,48.3f,-200};c.anchored=1;check(native_eyes::valid(c),"anchored pair rejected");
        bad=c;bad.anchor.y=std::numeric_limits<float>::infinity();check(!native_eyes::valid(bad),"infinite anchor accepted");
        bad=c;bad.anchored=2;check(!native_eyes::valid(bad),"unknown anchor mode accepted");
    });
    test("game's active view sits between the eyes and its lens holds both eye images", [] {
        native_eyes::Command c;c.enabled=1;c.serial=1;
        const Mat4 basis={1,0,0,0,0,-1,0,0,0,0,-1,0,100,50,-200,1};
        // Headset-like eyes: each is wider on its outer side.
        const float fovs[2][4]={{-.9f,.7f,-.85f,.75f},{-.7f,.9f,-.85f,.75f}};
        for(unsigned i=0;i<2;++i) {
            c.eyes[i].world=native_view::relativePose(basis,{{i? .032f:-.032f,0,0},{}});
            std::copy(std::begin(fovs[i]),std::end(fovs[i]),c.eyes[i].fov);
        }
        Mat4 pose{};native_eyes::Bounds lens{};
        check(native_eyes::headView(c,pose,lens,1),"parallel eyes rejected");
        for(int i=0;i<16;++i)near(pose[i],basis[i]);
        near(lens.left,std::tan(-.9f));near(lens.right,std::tan(.9f));
        near(lens.top,-std::tan(.75f));near(lens.bottom,std::tan(.85f));
        check(native_eyes::headView(c,pose,lens),"default margin rejected");
        near(lens.right,std::tan(.9f)*1.05f);near(lens.top,-std::tan(.75f)*1.05f);
        // Outward-canted eyes: every corner of each eye image stays inside the head lens.
        for(unsigned i=0;i<2;++i)
            c.eyes[i].world=native_view::relativePose(basis,{{i? .032f:-.032f,0,0},Quat::yaw(i? -.2f:.2f)});
        check(native_eyes::headView(c,pose,lens,1),"canted eyes rejected");
        check(native_view::validPose(pose),"head pose not orthonormal");
        near(pose[12],100);near(pose[13],50);near(pose[14],-200);
        const Vec3 x{pose[0],pose[1],pose[2]},y{pose[4],pose[5],pose[6]},z{pose[8],pose[9],pose[10]};
        for(const auto& eye:c.eyes)
            for(float h:{std::tan(eye.fov[0]),std::tan(eye.fov[1])})
                for(float v:{-std::tan(eye.fov[3]),-std::tan(eye.fov[2])}) {
                    const auto& m=eye.world;
                    const Vec3 d=Vec3{m[0],m[1],m[2]}*h+Vec3{m[4],m[5],m[6]}*v+Vec3{m[8],m[9],m[10]};
                    const float u=dot(d,x)/dot(d,z),w=dot(d,y)/dot(d,z);
                    check(u>=lens.left-1e-4f&&u<=lens.right+1e-4f&&w>=lens.top-1e-4f&&w<=lens.bottom+1e-4f,
                          "eye image corner outside the head lens");
                }
        check(lens.right-lens.left>2*std::tan(.9f)+.1f,"cant did not widen the head lens");
        auto bad=c;bad.eyes[1].world=native_view::relativePose(basis,{{.032f,0,0},Quat::yaw(3.f)});
        check(!native_eyes::headView(bad,pose,lens),"backward-facing eye accepted");
    });
    test("game HUD panel sits in front of the head at the width asked for, its markers on it", [] {
        // A head looking along world -z (rows right, down, forward, position).
        const Mat4 head{1,0,0,0, 0,-1,0,0, 0,0,-1,0, 10,20,30,1};
        const native_hud::Shape shape;
        // The game's own placement on October 9: 20 m from its camera, whose view is 0.8125 half wide.
        const Vec3 game{14.4446f,10.682f,11.f};
        Mat4 m{};
        check(native_hud::panel(head,game,20,.8125f,shape,m),"panel rejected");
        near(m[12],10);near(m[13],20);near(m[14],30-shape.distance);
        // x right, y up, z toward the viewer, every scale changed alike.
        const float k=m[0]/game.x;
        near(m[5],game.y*k);near(m[10],game.z*k);
        near(m[1],0);near(m[2],0);near(m[4],0);near(m[6],0);near(m[8],0);near(m[9],0);
        // What the game's view showed of it (16.25 m half wide at 20 m) now spans halfWidth.
        near(k*.8125f*20,shape.halfWidth*shape.distance);
        check(!native_hud::panel(head,{0,1,1},20,.8125f,shape,m),"a flat panel accepted");
        check(!native_hud::panel(head,game,20,0,shape,m),"no view width accepted");
        check(!native_hud::panel(head,game,std::numeric_limits<float>::quiet_NaN(),.8125f,shape,m),"NaN accepted");
        // Markers: straight ahead is the panel's middle, the panel's edges are its half width.
        float x{},y{};bool front{};
        check(native_hud::project(head,.577f,2,{10,20,0},.05f,x,y,front),"a point ahead rejected");
        near(x,.5f);near(y,.5f);check(front,"a point ahead is behind");
        check(native_hud::project(head,.577f,2,{10+.577f*10,20,20},.05f,x,y,front),"right edge rejected");
        near(x,1,.002f);near(y,.5f);
        check(native_hud::project(head,.577f,2,{10,20+.2885f*10,20},.05f,x,y,front),"top edge rejected");
        near(x,.5f);near(y,0,.002f);
        check(native_hud::project(head,.577f,2,{10,20,40},.05f,x,y,front)&&!front,"a point behind is ahead");
        // The panel's texture: the stream makes int(base * factor) pixels wide.
        for(uint32_t width:{1290u,1720u,2580u,3440u})
            check(static_cast<uint32_t>(static_cast<float>(native_hud::textureBase(width,1.35224f))*1.35224f)==width,
                  "texture base misses the window width");
        check(native_hud::textureBase(1920,1)==1920,"16:9 base changed");
    });
    test("game HUD panel holds still while the head looks around it, glides back beyond, stays upright", [] {
        const float degree=3.14159265f/180;
        const auto facing=[](const Quat& q){return q.rotate({0,0,-1});};
        native_hud::Follow follow;
        Quat panel=follow.update({},1.f/72);
        near(facing(panel).z,-1);check(!follow.gliding()&&follow.angle()<.01f,"the first update off the head");
        // A look 1.5 degrees aside, held for a second: inside the hold, the panel stays.
        for(int i=0;i<72;++i) panel=follow.update(Quat::yaw(1.5f*degree),1.f/72);
        near(facing(panel).x,0);near(follow.angle(),1.5f,.01f);
        // 10 degrees: it glides after the head and holds again once there.
        panel=follow.update(Quat::yaw(10*degree),1.f/72);
        check(follow.gliding()&&follow.angle()<10&&follow.angle()>8,"no glide past the hold");
        for(int i=0;i<72;++i) panel=follow.update(Quat::yaw(10*degree),1.f/72);
        check(!follow.gliding()&&follow.angle()<follow.settle,"the glide did not settle on the head");
        near(std::atan2(-facing(panel).x,-facing(panel).z),10*degree,.005f);
        // Most of the way in one glide time (63%), and none of a frame past a quarter second.
        native_hud::Follow timed;
        timed.update({},1.f/72);
        panel=timed.update(Quat::yaw(30*degree),timed.glide);
        near(std::atan2(-facing(panel).x,-facing(panel).z),30*degree*(1-std::exp(-1.f)),.002f);
        native_hud::Follow stalled;
        stalled.update({},1.f/72);
        panel=stalled.update(Quat::yaw(30*degree),5.f);
        near(std::atan2(-facing(panel).x,-facing(panel).z),30*degree*(1-std::exp(-.25f/stalled.glide)),.002f);
        // A head looking down and tilted over: the panel stays level, facing where the face does.
        native_hud::Follow tilted;
        panel=tilted.update(Quat::yaw(.5f)*Quat::around({1,0,0},-.3f)*Quat::around({0,0,1},.4f),1.f/72);
        near(panel.rotate({1,0,0}).y,0);
        near(facing(panel).y,std::sin(-.3f),.002f);
        near(std::atan2(-facing(panel).x,-facing(panel).z),.5f,.002f);
        // Straight down, the top of the head says which way: no NaN, and the panel faces down.
        native_hud::Follow down;
        panel=down.update(Quat::yaw(.7f)*Quat::around({1,0,0},-1.5707963f),1.f/72);
        check(std::isfinite(panel.x+panel.y+panel.z+panel.w),"straight down gave NaN");
        near(facing(panel).y,-1,.002f);
        // A reset starts in front of the head again, without a glide.
        follow.reset();
        panel=follow.update(Quat::yaw(-40*degree),1.f/72);
        near(std::atan2(-facing(panel).x,-facing(panel).z),-40*degree,.002f);
        check(!follow.gliding(),"a reset glided");
        // Seen from the head: a panel turned 10 degrees left of a head looking along world -z.
        const Mat4 head{1,0,0,0, 0,-1,0,0, 0,0,-1,0, 10,20,30,1};
        const Mat4 turned=native_hud::turned(head,Quat::yaw(10*degree));
        near(turned[8],-std::sin(10*degree));near(turned[9],0);near(turned[10],-std::cos(10*degree));
        near(turned[0],std::cos(10*degree));near(turned[2],-std::sin(10*degree));near(turned[5],-1);
        near(turned[12],10);near(turned[13],20);near(turned[14],30);
        check(native_hud::turned(head,{})==head,"no turn moved the pose");
        // The panel in front of the turned pose, and a point straight along it in its middle.
        Mat4 m{};
        check(native_hud::panel(turned,{14.4446f,10.682f,11.f},20,.8125f,native_hud::Shape{},m),"panel rejected");
        near(m[12],10-2*std::sin(10*degree));near(m[14],30-2*std::cos(10*degree));
        float x{},y{};bool front{};
        check(native_hud::project(turned,.577f,16.f/9,{10-50*std::sin(10*degree),20,30-50*std::cos(10*degree)},.05f,
                                  x,y,front)&&front,"a point along the panel rejected");
        near(x,.5f);near(y,.5f);
        // Sizes: degrees across, off none.
        near(native_hud::halfWidth(2),std::tan(30*degree));near(native_hud::halfWidth(1),std::tan(25*degree));
        near(native_hud::halfWidth(3),std::tan(35*degree));near(native_hud::halfWidth(0),0);
        near(native_hud::halfWidth(9),std::tan(30*degree));
    });
    test("web strand spans wrist to anchor and stays visible at range", [] {
        std::vector<Vertex> v;
        WebLine line;line.start={0,10,0};line.end={0,30,-60};line.splat=false;
        const Vec3 viewer{.2f,10.3f,.4f};
        appendWeb(v,line,viewer,.001f);
        check(!v.empty() && v.size()%3==0,"web produced no triangle list");
        const Vec3 axis=normalized(line.end-line.start);
        const float span=length(line.end-line.start);
        float lo=1e9f,hi=-1e9f,farWidth=0;
        for(const auto& p:v) {
            check(finite(p.position) && finite(p.color),"non-finite web vertex");
            const float s=dot(p.position-line.start,axis);
            lo=std::min(lo,s);hi=std::max(hi,s);
            if(s>span-.5f)farWidth=std::max(farWidth,length(p.position-(line.start+axis*s)));
            check(p.color.x<=1 && p.color.y<=1 && p.color.z<=1 && p.color.x>=0,"web color out of range");
        }
        near(lo,0,.02f);near(hi,span,.02f);
        // 1.6 pixels at about 63 m: wide enough not to break up into dashes.
        check(farWidth>=.5f*1.6f*.001f*62.f,"distant web thinner than its pixel floor");
        v.clear();line.extended=.5f;appendWeb(v,line,viewer,.001f);
        for(const auto& p:v)check(dot(p.position-line.start,axis)<=span*.5f+.1f,"unshot web drawn past its tip");
        v.clear();line.end=line.start;appendWeb(v,line,viewer,.001f);
        check(v.empty(),"zero-length web drew geometry");
        line.end={std::numeric_limits<float>::quiet_NaN(),0,0};appendWeb(v,line,viewer,.001f);
        check(v.empty(),"invalid web drew geometry");
    });
    test("slack web sags by its spare rope and splat surrounds the anchor", [] {
        std::vector<Vertex> taut,slack;
        WebLine line;line.start={0,20,0};line.end={20,20,0};line.splat=false;
        const Vec3 viewer{10,20,15};
        appendWeb(taut,line,viewer,.001f);
        line.slack=2;appendWeb(slack,line,viewer,.001f);
        auto lowest=[](const std::vector<Vertex>& v){float y=1e9f;for(const auto& p:v)y=std::min(y,p.position.y);return y;};
        near(lowest(taut),20,.03f);
        near(lowest(slack),20-std::sqrt(3.f*20*2/8),.05f);
        std::vector<Vertex> splat;line.slack=0;line.splat=true;appendWeb(splat,line,viewer,.001f);
        check(splat.size()>taut.size(),"anchor splat missing");
        float reach=0;
        for(size_t i=taut.size();i<splat.size();++i)reach=std::max(reach,length(splat[i].position-line.end));
        check(reach>.08f && reach<.3f,"anchor splat has the wrong size");
    });
    test("aim markers keep their size on screen and stand out on any background", [] {
        const Vec3 viewer{0,1.7f,0};
        // The angle a marker covers from the viewer.
        auto spread=[&](AimMark kind,Vec3 at,float squeeze=0,float radius=0){
            std::vector<Vertex> v;
            AimMarker m;m.kind=kind;m.point=at;m.squeeze=squeeze;m.radius=radius;
            appendAimMarker(v,m,viewer,.001f);
            check(!v.empty() && v.size()%3==0,"marker produced no triangle list");
            float widest=0;
            for(const auto& p:v){
                check(finite(p.position)&&finite(p.color),"non-finite marker vertex");
                widest=std::max(widest,length(p.position-at));
            }
            return widest/length(at-viewer);
        };
        for(const auto kind:{AimMark::anchor,AimMark::air,AimMark::blocked,AimMark::target}){
            const float close=spread(kind,{0,1.7f,-5}),far=spread(kind,{3,20,-50});
            near(close,far,close*.02f);
            // About 10-20 pixels across at a milliradian a pixel.
            check(close>.005f && close<.03f,"marker size off");
        }
        check(spread(AimMark::anchor,{0,1.7f,-10},.6f)<spread(AimMark::anchor,{0,1.7f,-10})*.8f,
              "squeezing the grip did not tighten the ring");
        check(spread(AimMark::target,{0,1.7f,-10},0,1.5f)*10>1.5f,"ring inside a large target");
        // Translucent and painted in order: a marker's soft dark shadows
        // first, then its colours, every edge fading out.
        auto dark=[](const Vertex& p){return p.color.x<.05f&&p.color.y<.05f&&p.color.z<.05f;};
        for(const auto kind:{AimMark::anchor,AimMark::air,AimMark::blocked,AimMark::target})
            for(const unsigned hand:{0u,1u}){
                std::vector<Vertex> v;AimMarker m;m.kind=kind;m.point={0,1.7f,-10};m.hand=hand;
                appendAimMarker(v,m,viewer,.001f);
                size_t lastDark=0,firstColour=v.size(),faded=0;
                for(size_t i=0;i<v.size();++i){
                    if(dark(v[i]))lastDark=i;else firstColour=std::min(firstColour,i);
                    check(v[i].alpha>=0&&v[i].alpha<=1,"marker opacity out of range");
                    faded+=v[i].alpha==0;
                }
                check(firstColour<v.size()&&lastDark<firstColour,"a shadow drawn over the marker's colour");
                check(faded>v.size()/4,"marker edges do not fade out");
            }
        // Each hand its own colour: the left hand's blue, the right hand's orange.
        auto hue=[&](unsigned hand){
            std::vector<Vertex> v;AimMarker m;m.point={0,1.7f,-10};m.hand=hand;appendAimMarker(v,m,viewer,.001f);
            float warm=0;size_t coloured=0;
            for(const auto& p:v)if(!dark(p)){warm+=p.color.x-p.color.z;++coloured;}
            return warm/static_cast<float>(std::max<size_t>(coloured,1));
        };
        check(hue(0)<-.5f&&hue(1)>.5f,"the hands' markers share a colour");
        // A fading marker is as translucent as it is faded; a gone one draws nothing.
        {
            std::vector<Vertex> v;AimMarker m;m.point={0,1.7f,-10};m.opacity=.5f;appendAimMarker(v,m,viewer,.001f);
            float most=0;for(const auto& p:v)most=std::max(most,p.alpha);
            near(most,.5f,1e-4f);
        }
        // The target's ring is round: its outer edge runs along most of a
        // circle (square corners would only reach out at four places), and
        // it closes in from wider while it locks on. The right hand's ring
        // is wider than the left's, so both on one target stay apart.
        auto outerAngles=[&](AimMarker m){
            std::vector<Vertex> v;appendAimMarker(v,m,viewer,.001f);
            float widest=0;for(const auto& p:v)widest=std::max(widest,length(p.position-m.point));
            std::array<bool,36> bins{};
            for(const auto& p:v){
                const Vec3 d=p.position-m.point;
                if(length(d)>=widest*.97f)bins[static_cast<size_t>((std::atan2(d.y,d.x)+3.1415927f)/(2*3.1415927f)*36)%36]=true;
            }
            return std::count(bins.begin(),bins.end(),true);
        };
        AimMarker t;t.kind=AimMark::target;t.point={0,1.7f,-10};t.radius=.6f;
        check(outerAngles(t)>=18,"the target's ring is not round");
        auto ringSize=[&](AimMarker m){
            std::vector<Vertex> v;appendAimMarker(v,m,viewer,.001f);
            float widest=0;for(const auto& p:v)widest=std::max(widest,length(p.position-m.point));
            return widest;
        };
        const float settled=ringSize(t);
        t.lock=0;check(ringSize(t)>settled*1.5f,"the target's ring does not close in");
        t.lock=1;t.hand=1;check(ringSize(t)>settled*1.1f,"both hands' rings on one target coincide");
        std::vector<Vertex> v;AimMarker m;m.point=viewer;appendAimMarker(v,m,viewer,.001f);
        m.point={0,std::numeric_limits<float>::quiet_NaN(),0};appendAimMarker(v,m,viewer,.001f);
        m.point={0,1.7f,-10};appendAimMarker(v,m,viewer,0);
        m.opacity=0;appendAimMarker(v,m,viewer,.001f);
        check(v.empty(),"invalid marker drew geometry");
    });
    test("aim marker motion steadies hand tremor, keeps up with a sweep and ignores turns", [] {
        constexpr float dt=1.f/72,degree=3.1415927f/180;
        auto ray=[](float yaw){return Quat::yaw(yaw).rotate({0,0,-1});};
        auto angle=[](Vec3 a,Vec3 b){return std::acos(std::clamp(dot(normalized(a),normalized(b)),-1.f,1.f));};
        // A hand held still with a 9 Hz tremor of 0.15 degrees.
        AimMarkerMotion motion;
        float lowest=1e9f,highest=-1e9f;
        for(int i=0;i<108;++i){
            motion.begin(1'000'000'000+static_cast<int64_t>(i*dt*1e9));
            const float wobble=.15f*degree*std::sin(2*3.1415927f*9*i*dt);
            const Vec3 d=motion.aim(ray(wobble),0);
            if(i>=36){const float yaw=std::atan2(-d.x,-d.z);lowest=std::min(lowest,yaw);highest=std::max(highest,yaw);}
        }
        check(highest-lowest<.5f*.3f*degree,"tremor not steadied");
        // A sweep at 2 radians a second: a few tenths of a degree behind.
        motion.reset();
        Vec3 last{};float yaw=0;
        for(int i=0;i<30;++i){
            motion.begin(1'000'000'000+static_cast<int64_t>(i*dt*1e9));
            yaw=i<6?0.f:2*(i-6)*dt;
            last=motion.aim(ray(yaw),0);
        }
        check(angle(last,ray(yaw))<.6f*degree,"the marker lags a sweep");
        // A snap turn turns the world and the aim with it: no motion.
        const int64_t t=1'000'000'000+static_cast<int64_t>(30*dt*1e9);
        motion.begin(t);
        const Vec3 turned=motion.aim(ray(yaw+.5236f),.5236f);
        check(angle(turned,ray(yaw+.5236f))<angle(last,ray(yaw))+.01f*degree,"a snap turn swung the marker");
        // The same image again changes nothing; after a long gap it starts afresh.
        motion.begin(t);
        check(angle(motion.aim(ray(yaw+.5236f),.5236f),turned)<1e-6f,"the same image moved the marker");
        motion.begin(t+500'000'000);
        check(angle(motion.aim(ray(1.f),0),ray(1.f))<1e-6f,"a gap did not restart the steadying");
        // Bad input passes through, untouched.
        const Vec3 bad{0,std::numeric_limits<float>::quiet_NaN(),0};
        check(!finite(motion.aim(bad,0)),"a bad direction was steadied");
    });
    test("aim marker motion fades markers in and out and eases them to new places", [] {
        constexpr float dt=1.f/72;
        const Vec3 origin{0,1.5f,0},ahead{0,0,-1};
        AimMarkerMotion motion;
        int frame=0;
        auto step=[&](const AimMarker* wanted,Vec3 direction=Vec3{0,0,-1}){
            motion.begin(2'000'000'000+static_cast<int64_t>(frame++*dt*1e9));
            std::vector<AimMarker> out;motion.markers(wanted,origin,direction,out);return out;
        };
        auto find=[](const std::vector<AimMarker>& v,AimMark kind)->const AimMarker*{
            for(const auto& m:v)if(m.kind==kind)return &m;
            return nullptr;
        };
        AimMarker anchor;anchor.point=origin+ahead*10;anchor.hand=1;
        auto shown=step(&anchor);
        check(shown.size()==1&&shown[0].opacity>.3f&&shown[0].opacity<.5f&&shown[0].hand==1,"a new marker did not show at once");
        near(length(shown[0].point-origin),10,1e-3f);
        for(int i=0;i<3;++i)shown=step(&anchor);
        near(shown[0].opacity,1);
        // A new distance along the ray: eased, not a jump, and there soon.
        anchor.point=origin+ahead*40;
        shown=step(&anchor);
        const float between=length(shown[0].point-origin);
        check(between>10.5f&&between<39,"the marker jumped to its new distance");
        for(int i=0;i<11;++i)shown=step(&anchor);
        near(length(shown[0].point-origin),40,.4f);
        // Another kind, the hand turned: the new one fades in on the new ray
        // while the old one fades out where it was.
        const Vec3 anchorAt=shown[0].point,turned=normalized(Vec3{.2f,0,-1});
        AimMarker air;air.kind=AimMark::air;air.point=origin+turned*100;
        shown=step(&air,turned);
        check(find(shown,AimMark::anchor)&&find(shown,AimMark::air),"a switch of kind did not fade across");
        check(find(shown,AimMark::anchor)->opacity<1&&find(shown,AimMark::air)->opacity<.5f,"the switch did not fade");
        check(length(find(shown,AimMark::anchor)->point-anchorAt)<1e-4f,"a fading marker followed the hand");
        near(length(cross(find(shown,AimMark::air)->point-origin,turned)),0,1e-3f);
        for(int i=0;i<7;++i)shown=step(&air,turned);
        check(!find(shown,AimMark::anchor)&&find(shown,AimMark::air)->opacity==1,"the old kind did not fade out");
        // A target: its ring locks on from wider and slides to the next target.
        AimMarker target;target.kind=AimMark::target;target.point={2,1,-8};target.radius=.4f;
        shown=step(&target);
        check(find(shown,AimMark::target)->lock<.1f,"the target's ring did not start wide");
        const float spin=find(shown,AimMark::target)->spin;
        for(int i=0;i<10;++i)shown=step(&target);
        near(find(shown,AimMark::target)->lock,1);
        check(find(shown,AimMark::target)->spin!=spin,"the target's ring does not turn");
        target.point={4,1,-8};
        shown=step(&target);
        const float slid=find(shown,AimMark::target)->point.x;
        check(slid>2.05f&&slid<3.5f,"the target's ring jumped to the next target");
        for(int i=0;i<14;++i)shown=step(&target);
        near(find(shown,AimMark::target)->point.x,4,.02f);
        // Nothing wanted: every marker fades out.
        shown=step(nullptr);
        check(!shown.empty()&&shown[0].opacity<1,"a marker vanished instead of fading");
        for(int i=0;i<6;++i)shown=step(nullptr);
        check(shown.empty(),"a marker did not fade out");
    });
    test("a surface marker follows the aim line across the surface's plane", [] {
        // A wall facing +z at z = -20, met at its origin; the hand now aims a little to the right.
        const Vec3 hit{0,0,-20},normal{0,0,1},origin{};
        const Vec3 direction=normalized(Vec3{.05f,0,-1});
        const Vec3 p=onAimLine(hit,normal,origin,direction);
        near(p.z,-20);near(p.x,1);near(length(cross(p-origin,direction)),0,.001f);
        // Along the wall, far off the hit, without a normal or behind the hand: the hit itself.
        check(length(onAimLine(hit,{1,0,0},origin,direction)-hit)<1e-5f,"grazing plane used");
        check(length(onAimLine(hit,normal,origin,normalized(Vec3{.9f,0,-1}))-hit)<1e-5f,"far crossing used");
        check(length(onAimLine(hit,{},origin,direction)-hit)<1e-5f,"missing normal used");
        check(length(onAimLine(hit,normal,origin,{0,0,1})-hit)<1e-5f,"crossing behind the hand used");
    });
    test("a web in both hands: the game's rope comes from the lower wrist to the upper one, bowing as its slack says", [] {
        Vec3 points[8];
        // A web across, and one straight up and down between the hands.
        for(const auto& [lower,upper]:{std::pair{Vec3{.2f,1.1f,.4f},Vec3{-.25f,1.3f,.45f}},
                                       std::pair{Vec3{.1f,1.1f,.4f},Vec3{.1f,1.6f,.4f}}}){
            const Vec3 along=normalized(upper-lower);
            // Taut: the straight line, its ends exactly the two wrists.
            native_webs::heldPoints(lower,upper,0,8,points);
            near(length(points[0]-lower),0,1e-6f);
            near(length(points[7]-upper),0,1e-6f);
            for(int i=1;i<7;++i)near(length(cross(points[i]-lower,along)),0,1e-5f);
            // Slack: off the line, about that much longer than it, and never above it.
            for(const float slack:{.05f,.2f}){
                native_webs::heldPoints(lower,upper,slack,8,points);
                float run=0;
                for(int i=1;i<8;++i)run+=length(points[i]-points[i-1]);
                near(run-length(upper-lower),slack,slack*.25f);
                for(int i=1;i<7;++i){
                    const Vec3 on=lower+(upper-lower)*(i/7.f);
                    check(length(points[i]-on)>.005f && points[i].y<=on.y+1e-5f,"a slack web does not bow below its line");
                }
            }
        }
        // Far more slack than half a metre's bow: no further than that; hands together: half of it hangs.
        const Vec3 lower{.2f,1.1f,.4f},upper{-.25f,1.3f,.45f};
        native_webs::heldPoints(lower,upper,3,8,points);
        for(int i=0;i<8;++i)check(length(points[i]-(lower+(upper-lower)*(i/7.f)))<.5f+1e-4f,"the web bowed beyond half a metre");
        native_webs::heldPoints(lower,lower,.3f,5,points);
        near(lower.y-points[2].y,.15f,1e-4f);
    });
    test("a web round edges: the game's rope is laid from the wrist round each of them, the nearest first", [] {
        Vec3 points[8];
        const Vec3 wrist{0,1.4f,0},lower{.1f,1.1f,.2f};
        native_webs::Bent bent;
        // Straight: nothing to lay but the wrist.
        check(native_webs::ropePoints(wrist,nullptr,0,bent,8,points)==0,"a straight web was laid");
        bent.count=2;bent.points[0]={3,10,-2};bent.points[1]={8,15,-6};
        int upper=-1;
        check(native_webs::ropePoints(wrist,nullptr,0,bent,8,points,&upper)==3&&upper==0,"not the wrist and two edges");
        near(length(points[0]-wrist),0);near(length(points[1]-bent.points[0]),0);near(length(points[2]-bent.points[1]),0);
        // In both hands as well: the bow to the upper wrist first, then the edges.
        check(native_webs::ropePoints(wrist,&lower,.2f,bent,8,points,&upper)==8&&upper==5,"not the bow and the edges");
        near(length(points[0]-lower),0);near(length(points[5]-wrist),0);near(length(points[7]-bent.points[1]),0);
        // Six edges and the bow: the bow keeps three points; edges past the room are left out.
        bent.count=6;
        for(int k=0;k<6;++k)bent.points[k]={float(k),20,0};
        check(native_webs::ropePoints(wrist,&lower,.2f,bent,8,points,&upper)==8&&upper==2,"the bow lost its points");
        near(length(points[7]-bent.points[4]),0);
    });
    test("a web taken hold of gets no rope of its own: the web's rope draws it, from the nearer hand", [] {
        using Hand=WebRopes::Hand;
        WebRopes r;
        // The left hand shoots a web: the left rope, a new one, from the left wrist.
        r.update({Hand{true,false,false,19.4f,100},Hand{}});
        check(r.rope(0).hands==1&&r.rope(0).key==100&&r.rope(0).from==0&&!r.rope(1).hands,"a shot web has no rope of its side");
        check(r.slot(0)==0&&r.slot(1)==-1&&r.upper()==-1,"one hand's web is in both, or in none");
        // The right hand takes hold of it 0.3 m nearer its end: the same rope, no new one, now from the right wrist.
        r.update({Hand{true,false,true,19.4f,100},Hand{true,true,true,19.1f,200}});
        check(r.rope(0).hands==3&&r.rope(0).key==100&&!r.rope(1).hands,"taking hold of a web made a rope");
        check(r.slot(1)==0&&r.upper()==1&&r.rope(0).from==1,"the strand does not come to the hand nearer its end first");
        // The hands move as they like: nothing changes hands. A reel that makes the other rope the shorter does.
        r.update({Hand{true,false,true,19.4f,100},Hand{true,true,true,19.1f,200}});
        check(r.upper()==1,"the web changed hands with no hold changed");
        r.update({Hand{true,false,true,18.9f,100},Hand{true,true,true,19.1f,200}});
        check(r.upper()==0&&r.rope(0).from==0&&r.rope(0).key==100,"the shorter rope is not the nearer hand");
        r.update({Hand{true,false,true,19.4f,100},Hand{true,true,true,19.1f,200}});
        // The hand that shot it lets go: the rope stays, from the hand that has the web.
        r.update({Hand{},Hand{true,true,false,19.1f,200}});
        check(r.rope(0).hands==2&&r.rope(0).key==100&&r.rope(0).from==1&&r.slot(1)==0&&r.upper()==-1,
              "the web lost its rope with the hand that shot it");
        // That hand shoots a new web: the free rope, the other side's, and its own key.
        r.update({Hand{true,false,false,30,300},Hand{true,true,false,19.1f,200}});
        check(r.rope(1).hands==1&&r.rope(1).key==300&&r.rope(1).from==0&&r.rope(0).hands==2&&r.slot(0)==1,
              "a new web took the held web's rope, or none");
        // The held web is let go of: its rope ends, and remembers whose it was (a let-go web trails from it).
        r.update({Hand{true,false,false,30,300},Hand{}});
        check(!r.rope(0).hands&&r.rope(0).key==100&&r.rope(0).last==1&&r.rope(0).from==-1,"a web let go of kept its rope");
        // A web shot anew between two frames is another web: its rope's key changes.
        r.update({Hand{true,false,false,25,400},Hand{}});
        check(r.rope(0).hands==1&&r.rope(0).key==400&&!r.rope(1).hands,"a web shot anew kept the old web's rope");
        // Both appear at once (focus back), one of them a hold on the other's: still one rope.
        WebRopes both;
        both.update({Hand{true,true,true,19.1f,500},Hand{true,false,true,19.4f,450}});
        check(both.rope(1).hands==3&&both.rope(1).key==450&&!both.rope(0).hands&&both.upper()==0,
              "two holds on one web seen at once made two ropes");
        r.clear();
        check(!r.rope(0).hands&&r.slot(0)==-1,"cleared ropes still draw");
    });
    test("hands: the web button makes the web-shooting hand; a web that missed keeps it while held", [] {
        HandPoser p;HandPoseInput in;in.tracked=true;
        const float dt=1.f/90;
        HandPose pose;
        // The button part of the way down: the two fingers start for the trigger.
        in.web=in.squeeze=.3f;
        for(int i=0;i<20;++i)pose=p.update(dt,in);
        near(pose.shoot,.3f/.65f,.01f);near(pose.fist,.3f,.001f);
        // Pressed: the whole pose within 0.1 s, though the grip is clenched, and still after five seconds.
        in.web=in.squeeze=1;
        for(int i=0;i<9;++i)pose=p.update(dt,in);
        near(pose.shoot,1,.001f);near(pose.fist,1,.001f);
        for(int i=0;i<450;++i)pose=p.update(dt,in);
        near(pose.shoot,1,.001f);
        // Half let go, it is held still (the web is); let go, the hand opens.
        in.web=in.squeeze=.5f;
        for(int i=0;i<20;++i)pose=p.update(dt,in);
        near(pose.shoot,1,.001f);
        in.web=in.squeeze=0;
        for(int i=0;i<45;++i)pose=p.update(dt,in);
        near(pose.shoot,0,.001f);near(pose.fist,0,.001f);
    });
    test("hands: a hand that takes the other hand's web grips it; a web ball shoots; a punch is a fist", [] {
        const float dt=1.f/90;
        HandPose pose;
        // On the other hand's web the button closes a fist, on the way down and held.
        HandPoser grip;HandPoseInput on;on.tracked=on.gripping=true;on.web=on.squeeze=.4f;
        for(int i=0;i<20;++i)pose=grip.update(dt,on);
        near(pose.shoot,0,.001f);near(pose.fist,.4f,.001f);
        on.web=on.squeeze=1;
        for(int i=0;i<90;++i)pose=grip.update(dt,on);
        near(pose.shoot,0,.001f);near(pose.fist,1,.001f);
        // With the trigger as the web button the grip is open: the hand closes on the web all the same.
        HandPoser swapped;HandPoseInput trigger;trigger.tracked=trigger.gripping=true;trigger.web=1;
        for(int i=0;i<30;++i)pose=swapped.update(dt,trigger);
        near(pose.fist,1,.001f);near(pose.shoot,0,.001f);
        // A web ball from an open hand: the pose for half a second.
        HandPoser ball;HandPoseInput in;in.tracked=true;in.shot=true;
        pose=ball.update(dt,in);in.shot=false;
        for(int i=0;i<5;++i)pose=ball.update(dt,in);
        near(pose.shoot,1,.001f);near(pose.fist,0,.001f);
        for(int i=0;i<34;++i)pose=ball.update(dt,in);
        near(pose.shoot,1,.001f);
        for(int i=0;i<30;++i)pose=ball.update(dt,in);
        near(pose.shoot,0,.001f);
        // A fast hand shoots as the shooting hand; with somebody to punch it is a fist.
        HandPoser fast;HandPoseInput thrown;thrown.tracked=true;thrown.web=thrown.squeeze=1;thrown.speed=3;
        for(int i=0;i<5;++i)pose=fast.update(dt,thrown);
        near(pose.shoot,1,.001f);near(pose.fist,1,.001f);
        HandPoser fight;thrown.fighting=true;
        for(int i=0;i<5;++i)pose=fight.update(dt,thrown);
        near(pose.shoot,0,.001f);near(pose.fist,1,.001f);
        // An untracked hand is neither.
        thrown.tracked=false;pose=fight.update(dt,thrown);
        near(pose.fist,0);near(pose.shoot,0);
    });
    test("hands: a web that holds closes the hand on it once the shot is there", [] {
        // "when web connects and held lets change the hand to fist and not keep the horn pose"
        const float dt=1.f/90;
        HandPose pose;
        // Pressed, the web holding from that frame: the pose whole while the shot is on its way (0.1 s).
        HandPoser p;HandPoseInput in;in.tracked=true;in.web=in.squeeze=1;in.holding=true;
        for(int i=0;i<9;++i)pose=p.update(dt,in);
        near(pose.shoot,1,.001f);near(pose.fist,1,.001f);
        // There: the hand closes on the web, a fist 0.4 s after the shot, and for as long as it is held.
        for(int i=0;i<27;++i)pose=p.update(dt,in);
        near(pose.shoot,0,.001f);near(pose.fist,1,.001f);
        for(int i=0;i<450;++i)pose=p.update(dt,in);
        near(pose.shoot,0,.001f);near(pose.fist,1,.001f);
        // Let go, the hand opens; the next web makes the pose again.
        in.web=in.squeeze=0;in.holding=false;
        for(int i=0;i<45;++i)pose=p.update(dt,in);
        near(pose.shoot,0,.001f);near(pose.fist,0,.001f);
        in.web=in.squeeze=1;in.holding=true;
        for(int i=0;i<9;++i)pose=p.update(dt,in);
        near(pose.shoot,1,.001f);
        // With the trigger as the web button the grip is open: the hand closes on the web all the same.
        HandPoser swapped;HandPoseInput trigger;trigger.tracked=trigger.holding=true;trigger.web=1;
        for(int i=0;i<9;++i)pose=swapped.update(dt,trigger);
        near(pose.shoot,1,.001f);near(pose.fist,0,.001f);
        for(int i=0;i<36;++i)pose=swapped.update(dt,trigger);
        near(pose.shoot,0,.001f);near(pose.fist,1,.001f);
        // A web that missed is no hold: the pose stays while the button is.
        HandPoser missed;HandPoseInput miss;miss.tracked=true;miss.web=miss.squeeze=1;
        for(int i=0;i<90;++i)pose=missed.update(dt,miss);
        near(pose.shoot,1,.001f);
    });
    test("web timeline animates the shot and the release", [] {
        WebTimeline t;
        t.update(0,true,{1,2,3},{0,1,0},1000);
        check(t[0].attached && t[0].attachedAt==1000,"attachment time lost");
        t.update(0,true,{1,2,4},{0,1,1},2000);
        check(t[0].attachedAt==1000,"held web restarted its shot");
        near(t[0].anchor.z,4);
        t.update(0,false,{},{},3000);
        check(!t[0].attached && t[0].releasedAt==3000,"release time lost");
        near(t[0].anchor.z,4);near(t[0].wrist.z,1);
        near(WebAnimation::extended(0),0);near(WebAnimation::extended(.045f),.5f);near(WebAnimation::extended(1),1);
        WebLine line;
        check(WebAnimation::released(.05f,{0,0,0},{0,0,-10},line),"release animation missing");
        check(line.start.z<0 && line.start.z>-10,"released end did not move toward the anchor");
        check(!WebAnimation::released(.5f,{0,0,0},{0,0,-10},line),"release animation never ended");
        check(!WebAnimation::released(-.01f,{0,0,0},{0,0,-10},line),"release animation started early");
    });
    test("render-frame anchor moves tracked eyes with the player, not the head", [] {
        // The worker samples the player before the frame it renders. Eyes from
        // the stale sample plus the player's travel must equal eyes from the
        // rendered position, so the camera stays fixed relative to the body.
        auto f=trackedFrame();f.head.orientation={0,.38268343f,0,.92387953f};
        GameTrackingRig stale, current;
        const Vec3 sampled{10,20,30}, rendered{10.9f,20.1f,29.6f};
        const auto a=stale.update(f,sampled,{0,0,-1},true), b=current.update(f,rendered,{0,0,-1},true);
        near(a.anchor.x,sampled.x);near(b.anchor.z,rendered.z);
        Vec3 offset{};
        check(native_eyes::reanchorOffset(a.anchor,rendered,offset),"normal travel rejected");
        for(unsigned eye=0;eye<2;++eye) {
            near(a.eyes[eye][12]+offset.x,b.eyes[eye][12]);
            near(a.eyes[eye][13]+offset.y,b.eyes[eye][13]);
            near(a.eyes[eye][14]+offset.z,b.eyes[eye][14]);
            for(int i=0;i<12;++i)near(a.eyes[eye][i],b.eyes[eye][i]);
        }
        check(!native_eyes::reanchorOffset(a.anchor,{60,20,30},offset),"teleport treated as travel");
        near(length(offset),0);
        check(!native_eyes::reanchorOffset({std::numeric_limits<float>::quiet_NaN(),0,0},rendered,offset),
              "invalid sample accepted");
    });
    test("GPU eye pair requires the same pose and render generation", [] {
        EyePairState p;
        p.begin(0,{5,10});p.end(0,{5,10});p.begin(1,{5,11});p.end(1,{5,11});
        check(!p.ready().serial,"mixed scene generations accepted");
        p.begin(0,{5,11});p.end(0,{5,11});check(p.ready().generation==11,"matching pair rejected");
        p.begin(0,{5,12});check(!p.ready().serial,"partly overwritten old image accepted");
        p.end(0,{5,11});check(!p.ready().serial,"out of order completion accepted");
        p.end(0,{5,12});p.begin(1,{6,12});p.end(1,{6,12});
        check(!p.ready().serial,"different predicted poses accepted");
    });
    test("eye job table reclaims copies the game never rendered", [] {
        // The October 5 session stalled after about 10,600 frames: dropped eye
        // copies never ended, filled all 128 slots, and later frames went unmarked.
        EyeJobTable t;
        uint64_t reclaimed{};
        auto job=[](uint64_t address){return reinterpret_cast<const void*>(address*64);};
        for(uint64_t g=1;g<=20000;++g)
            for(unsigned eye=0;eye<2;++eye) {
                const auto view=job(g*2+eye);
                reclaimed+=t.record({view,g,g,eye});
                const auto found=t.find(view);
                check(found&&found->generation==g&&found->eye==eye,"new eye job could not be marked");
                if(g%37)t.finish(view); // every 37th frame's copies are never rendered
            }
        check(reclaimed>=2*(20000/37)-2*EyeJobTable::capacity,"dropped copies were kept forever");
        EyeJobTable live;
        live.record({job(1),7,100,0});
        for(uint64_t g=101;g<=100+EyeJobTable::maxAge;++g)check(!live.record({job(g),7,g,1}),"job in flight reclaimed");
        check(live.find(job(1)),"recent unfinished job lost");
        check(live.record({job(999),7,101+EyeJobTable::maxAge,1})==1&&!live.find(job(1)),"stale job not reclaimed");
        check(!live.record({job(999),8,101+EyeJobTable::maxAge,0})&&live.find(job(999))->serial==8,
              "recopied job duplicated instead of updated");
        EyeJobTable full;
        for(uint64_t i=0;i<EyeJobTable::capacity;++i)full.record({job(i+1),1,50+i/64,0});
        check(full.record({job(5000),2,51,1})==1&&full.find(job(5000))&&!full.find(job(1)),
              "full table did not give up its oldest entry");
        check(!full.record({nullptr,3,52,0})&&!full.find(nullptr),"null job recorded");
    });
    test("eyes and game webs move from one hero sample per frame", [] {
        native_eyes::FrameHero h;
        check(!h.fresh(0,{1,2,3}),"sample used before any rope update");
        check(h.fresh(1,{1,2,3}),"new rope sample ignored");
        check(!h.fresh(1,{1,2,3}),"one rope sample reused by a later frame");
        check(!h.fresh(2,{std::numeric_limits<float>::quiet_NaN(),0,0}),"invalid sample accepted");
        check(!h.fresh(3,{2e6f,0,0}),"unbounded sample accepted");
        check(h.fresh(5,{4,5,6}),"sample after skipped updates ignored");
        // The rope update moves wrists from the sampled feet to its hero sample.
        // Eyes moved by the same sample keep the web on the wrist even when the
        // render transform has advanced a further 0.71 m (32 m/s for 22 ms).
        GameTrackingRig rig;
        const auto m=rig.update(trackedFrame(),{100,50,-20},{0,0,-1},true);
        const Vec3 rope{108.4f,49.1f,-21.3f}, render=rope+Vec3{.71f,0,0};
        Vec3 shared{}, late{};
        check(native_eyes::reanchorOffset(m.anchor,rope,shared)&&native_eyes::reanchorOffset(m.anchor,render,late),
              "normal travel rejected");
        const Vec3 eye{m.eyes[0][12],m.eyes[0][13],m.eyes[0][14]}, wrist=webShooter(m.grips[0],0).position;
        near(length((wrist+shared)-(eye+shared)-(wrist-eye)),0);
        near(length((wrist+shared)-(eye+late)-(wrist-eye)),.71f);
    });
    test("a head asked for before the game moves the player goes where the frame will draw him", [] {
        // The HUD projects its markers before the frame's gameplay. A head latched
        // there (0.2.8, 0.2.9) had the last frame's player: the monitor's view and
        // the HUD panel trailed the eyes by a frame of travel, 0.9 m in a swing.
        native_eyes::HeroTravel t;
        Vec3 hero{};
        check(!t.ahead(1,hero),"a player foreseen before any frame placed him");
        t.placed(7,{100,50,-20});
        check(t.ahead(8,hero),"the frame after a placed one not foreseen");
        near(length(hero-Vec3{100,50,-20}),0);
        // 27 m/s at 30 frames a second: each frame draws him 0.9 m on.
        t.placed(8,{100.9f,50,-20});
        check(t.ahead(9,hero),"a moving player not foreseen");
        near(length(hero-Vec3{101.8f,50,-20}),0);
        check(!t.ahead(8,hero)&&!t.ahead(10,hero),"a frame other than the next foreseen");
        // The eyes of frame 9 are placed from where he then is; the foreseen
        // head is there too, the one from frame 8's player 0.9 m behind.
        GameTrackingRig rig;
        const auto m=rig.update(trackedFrame(),{100.2f,50,-20},{1,0,0},true);
        const Vec3 drawn{101.8f,50,-20};
        Vec3 eyes{}, foreseen{}, stale{};
        check(native_eyes::reanchorOffset(m.anchor,drawn,eyes)&&native_eyes::reanchorOffset(m.anchor,hero,foreseen)&&
              native_eyes::reanchorOffset(m.anchor,{100.9f,50,-20},stale),"normal travel rejected");
        near(length(foreseen-eyes),0);near(length(stale-eyes),.9f);
        // A teleport (fast travel, a respawn) is not carried on, nor a frame
        // that follows one without a head.
        t.placed(9,{400,50,-20});
        check(t.ahead(10,hero),"the frame after a teleport not foreseen");
        near(length(hero-Vec3{400,50,-20}),0);
        t.placed(12,{401,50,-20});
        check(t.ahead(13,hero),"the frame after a gap not foreseen");
        near(length(hero-Vec3{401,50,-20}),0);
        t.placed(13,{std::numeric_limits<float>::quiet_NaN(),0,0});
        check(!t.ahead(14,hero),"an invalid position foreseen");
        t.placed(14,{402,50,-20});t.placed(15,{402.5f,50,-20});
        check(t.ahead(16,hero),"travel after an invalid position lost");
        near(length(hero-Vec3{403,50,-20}),0);
    });
    test("native ray batches reject unsafe geometry and expired commands", [] {
        native_rays::Command c;
        c.serial=1; c.count=2;
        c.rays[0]={{10,20,30},100,{0,-1,0},12};
        c.rays[1]={{10,20,30},100,{1,0,0},13};
        check(native_rays::valid(c),"valid rays rejected");
        auto bad=c; bad.count=9; check(!native_rays::valid(bad),"overflow accepted");
        bad=c; bad.leaseMs=0; check(!native_rays::valid(bad),"expired query accepted");
        bad=c; bad.leaseMs=251; check(!native_rays::valid(bad),"long lease accepted");
        bad=c; bad.rays[0].direction={0,-2,0}; check(!native_rays::valid(bad),"scaled ray accepted");
        bad=c; bad.rays[0].origin.x=std::numeric_limits<float>::infinity();
        check(!native_rays::valid(bad),"infinite origin accepted");
        bad=c; bad.rays[0].distance=151; check(!native_rays::valid(bad),"excessive range accepted");
        c.count=0; c.leaseMs=0; check(native_rays::valid(c),"cancel command rejected");
    });
    test("controller rays retain hand identity and disappear on tracking loss", [] {
        Input in;
        in.hands[0].tracked=true; in.hands[0].aim={{1,2,3},{}};
        in.hands[1].tracked=true; in.hands[1].aim={{-1,2,3},Quat::yaw(1.5707963f)};
        auto rays=controllerAimRays(in,7);
        check(native_rays::valid(rays) && rays.count==2,"controller batch invalid");
        near(rays.rays[0].origin.x,1); near(rays.rays[1].direction.x,-1);
        check(rays.rays[0].tag==0 && rays.rays[1].tag==1,"hands crossed");
        in.hands[0].tracked=false; rays=controllerAimRays(in,8);
        check(rays.count==1 && rays.rays[0].tag==1,"lost hand retained");
        in.focused=false; rays=controllerAimRays(in,9);
        check(rays.count==0 && rays.leaseMs==0 && native_rays::valid(rays),"focus loss retained aim");
    });
    test("web anchors require a live static collision body", [] {
        native_rays::Hit hit;
        hit.count=1; hit.bodyMatches=1; hit.bodyFlags=1; hit.motionId=0; hit.broadPhaseId=0xa0000104;
        check(native_rays::fixedSurface(hit),"captured static building rejected");
        auto bad=hit; bad.motionId=4; check(!native_rays::fixedSurface(bad),"moving body accepted");
        bad=hit; bad.bodyFlags=2; check(!native_rays::fixedSurface(bad),"dynamic body accepted");
        bad=hit; bad.bodyMatches=0; check(!native_rays::fixedSurface(bad),"reused ID accepted");
        bad=hit; bad.broadPhaseId=0xffffffff; check(!native_rays::fixedSurface(bad),"unloaded body accepted");
        bad=hit; bad.count=0; check(!native_rays::fixedSurface(bad),"missing hit accepted");
    });
    test("external collision prediction never commits an untested position", [] {
        TestWorld world; world.enabled=false;
        Swing swing(inert());
        Input in;
        Body actual{{10,20,30},{4,0,0},false};
        auto request=swing.predictNativeStep(.02f,in,world,actual);
        check(request.valid,"external step rejected");
        near(request.target.x,10.08f);
        near(swing.body().position.x,10);
        // A native wall blocks the request. Its next feedback must replace the
        // prediction, rather than letting hidden position/speed cross the wall.
        actual.velocity={};
        request=swing.predictNativeStep(.02f,in,world,actual);
        near(request.target.x,10);
        near(request.velocity.x,0);
    });
    test("external web release preserves flight velocity", [] {
        TestWorld world;
        Swing swing(inert());
        Body actual{{0,0,0},{5,0,0},false};
        auto in=aimed();
        auto request=swing.predictNativeStep(.01f,in,world,actual);
        check(swing.webs()[0].attached,"native prediction did not attach");
        actual.position=request.target; actual.velocity=request.velocity;
        const auto before=actual.velocity;
        in.hands[0].grip=0;
        request=swing.predictNativeStep(.01f,in,world,actual);
        check(!swing.webs()[0].attached,"release kept a rope force");
        near(length(request.velocity-before),0);
        near(length(request.target-(actual.position+before*.01f)),0);
    });
    test("external prediction keeps its web through a long step and relinquishes unfocused input", [] {
        TestWorld world; Swing swing;
        auto in=aimed(); Body actual{{0,0,0},{},false};
        check(swing.predictNativeStep(.01f,in,world,actual).valid,"initial native step rejected");
        check(swing.webs()[0].attached,"no web to release");
        // A slow game: the steps of the October 10 headset report, and longer.
        for(float dt:{.051f,.0665f,longestNativeStep,.25f}) {
            check(swing.predictNativeStep(dt,in,world,actual).valid,"a long native step was refused");
            check(swing.webs()[0].attached,"a long native step let go of the web");
        }
        in.focused=false;
        check(!swing.predictNativeStep(.01f,in,world,actual).valid,"unfocused native frame accepted");
        check(!swing.webs()[0].attached,"unfocused input held web");
        in.focused=true;
        check(!swing.predictNativeStep(0,in,world,actual).valid,"a step of no length accepted");
        check(!swing.predictNativeStep(std::numeric_limits<float>::quiet_NaN(),in,world,actual).valid,
              "a step of no known length accepted");
    });
    test("a native step longer than the longest moves the body as far as the longest", [] {
        TestWorld world; world.enabled=false;
        Swing swing(inert());
        Input in; Body actual{{0,0,0},{5,0,0},false};
        near(swing.predictNativeStep(.0665f,in,world,actual).target.x,5*.0665f);
        near(swing.predictNativeStep(longestNativeStep,in,world,actual).target.x,5*longestNativeStep);
        const auto request=swing.predictNativeStep(.25f,in,world,actual);
        near(request.target.x,5*longestNativeStep);
        near(request.velocity.x,5);
    });
    test("native input after a gap keeps the webs and makes no yank of the gap", [] {
        TestWorld world;
        Swing swing(inert());
        Body actual{{0,0,0},{},false};
        auto in=aimed();
        auto request=swing.predictNativeStep(.01f,in,world,actual,.01f);
        check(request.valid&&swing.webs()[0].attached,"no web to keep");
        actual.position=request.target;actual.velocity=request.velocity;
        // The hand came down half a metre while no input arrived: fast
        // enough for a yank over 0.2 s, but nothing measured it.
        in.hands[0].gripRelativeToHead.y-=.5f;
        request=swing.predictNativeStep(.01f,in,world,actual,.2f);
        check(request.valid&&swing.webs()[0].attached,"a gap in input let go of the web");
        for(const auto& event:swing.events())check(event.kind!=EventKind::Zip,"travel across the gap yanked");
        near(length(request.velocity),0);
        // The next samples measure the hand again.
        actual.position=request.target;actual.velocity=request.velocity;
        unsigned zips{};
        for(int i=0;i<3;++i) {
            in.hands[0].gripRelativeToHead.y-=.06f;
            request=swing.predictNativeStep(.01f,in,world,actual,.02f);
            for(const auto& event:swing.events())zips+=event.kind==EventKind::Zip;
            actual.position=request.target;actual.velocity=request.velocity;
        }
        check(zips==1,"a yank after the gap was missed");
    });
    test("input clock handles repeated and skipped controller frames", [] {
        game_swing::InputSampleClock clock;
        game_swing::Command c; c.serial=1; c.sampleTimeNs=1000000000; c.sampleSeconds=.01f;
        near(clock.consume(c),.01f);
        near(clock.consume(c),0);
        c.serial=4; c.sampleTimeNs+=30000000;
        near(clock.consume(c),.03f);
        c.serial=5; c.sampleTimeNs-=1;
        check(!std::isfinite(clock.consume(c)),"backdated pose accepted");
        clock.reset();
        near(clock.consume(c),.01f);
    });
    test("the swing lets the game have the hero for a move it was asked for, once the game has begun it", [] {
        using game_swing::AirYield;
        // The mover's steps and the game's air handler (Jump, Fall): twice a step through Spidy's flight.
        uint64_t steps=100,air=200,now=1000;
        AirYield y;
        const auto step=[&](bool asked,bool airRuns,bool owned=true,bool airborne=true,bool grasp=false){
            ++steps;now+=5;if(airRuns)air+=2;
            return y.update(now,asked,owned,airborne,grasp,steps,air);
        };
        // Flight as usual, the player asking: the game took nothing, the swing goes on.
        for(int i=0;i<20;++i)check(!step(true,true),"the swing let go of a hero the game had not taken");
        // Asked, and the game's air handler quiet for two steps: the game's move has him.
        check(!step(true,false),"one quiet step is the game's move already");
        check(step(false,false)&&y.began()&&y.yields()==1,"the game's move did not take the hero");
        check(step(false,false)&&!y.began(),"the hand-over began twice");
        // Back in the game's air, the move done: the swing may have him again (a web of the player's).
        check(!step(false,true),"the move's end kept the swing off");
        // Landing ends it too, and so do yieldMs.
        for(int i=0;i<3;++i)step(true,false);
        check(y.yielded(),"no second hand-over");
        check(!step(false,false,false,false),"landed, the swing still kept off");
        for(int i=0;i<3;++i)step(true,false);
        check(y.yielded() && y.yields()==3,"no third hand-over");
        now+=AirYield::yieldMs;
        check(!step(false,false),"the hand-over outlived yieldMs");
        // Quiet steps without an ask, or with one older than askMs, are nobody's move of the game's: nothing.
        AirYield late;
        uint64_t s2=10,a2=20,t2=0;
        late.update(t2,true,true,true,false,s2,a2);
        t2+=AirYield::askMs+1;
        for(int i=0;i<5;++i){++s2;t2+=5;check(!late.update(t2,false,true,true,false,s2,a2),"an old ask let go");}
        AirYield unasked;
        for(int i=0;i<5;++i){++s2;t2+=5;check(!unasked.update(t2,false,true,true,false,s2,a2),"no ask let go");}
        // Nor in the game's wall grasp (its own latch), nor where the swing has no hero, nor on the ground.
        AirYield other;
        for(int i=0;i<5;++i){++s2;t2+=5;check(!other.update(t2,true,true,true,true,s2,a2),"the grasp let go");}
        for(int i=0;i<5;++i){++s2;t2+=5;check(!other.update(t2,true,false,true,false,s2,a2),"nobody's hero let go");}
        for(int i=0;i<5;++i){++s2;t2+=5;check(!other.update(t2,true,true,false,false,s2,a2),"the ground let go");}
    });
    test("a stutter keeps the last focused input; a longer loss of it does not", [] {
        game_swing::InputHold hold;
        game_swing::Command c;c.serial=7;c.focused=1;c.hands[0].grip=1;
        check(hold.update(c,true,1000)&&c.serial==7,"live input refused");
        game_swing::Command lost;lost.serial=8;lost.focused=0;
        auto x=lost;
        check(hold.update(x,false,1000+controlHoldMs)&&x.serial==7&&x.focused&&x.hands[0].grip==1,
              "a stutter dropped the input");
        x=lost;
        check(!hold.update(x,false,1001+controlHoldMs),"held input outlived the stutter");
        x=lost;
        check(!hold.update(x,false,1002+controlHoldMs),"lost input came back without a live sample");
        c.serial=9;
        check(hold.update(c,true,5000)&&c.serial==9,"live input refused after a loss");
        hold.reset();x=lost;
        check(!hold.update(x,false,5001),"a cancelled swing kept its old input");
    });
    test("native yanks use the controller clock across repeated physics samples", [] {
        for (unsigned physicsHz : {120u,240u}) {
            TestWorld world;
            Swing swing(inert());
            Body actual{{0,0,0},{},false};
            auto in=aimed();
            unsigned zips{};
            const unsigned repeat=physicsHz/60;
            const float step=1.f/physicsHz;
            for(unsigned tick=0;tick<repeat*9;++tick) {
                const bool sampled=tick%repeat==0;
                if(sampled && tick) in.hands[0].gripRelativeToHead.y-=.03f;
                const auto request=swing.predictNativeStep(step,in,world,actual,sampled?1.f/60:0);
                check(request.valid,"valid controller clock rejected");
                for(const auto& event:swing.events()) zips+=event.kind==EventKind::Zip;
                actual.position=request.target; actual.velocity=request.velocity;
            }
            check(zips==1,"repeated physics samples interrupted or repeated the yank");
            near(actual.velocity.y,1.8f*inert().zipMultiplier,.01f);
        }
    });
    test("native landing keeps a short point launch window and pre-impact direction", [] {
        TestWorld world;
        Swing swing(inert());
        Body actual{{0,3,0},{3,0,0},false};
        auto in=aimed();
        swing.predictNativeStep(.01f,in,world,actual);
        for (unsigned i=0;i<6;++i) {
            in.hands[0].gripRelativeToHead.y-=.03f;
            swing.predictNativeStep(.01f,in,world,actual);
        }
        actual={{0,.35f,0},{},true};
        in.hands[0].grip=0;
        swing.predictNativeStep(.01f,in,world,actual);
        check(swing.pointLaunchReady(),"native landing lost the zip window");
        in.jump=true;
        const auto launch=swing.predictNativeStep(.01f,in,world,actual);
        bool event{};
        for (const auto& e:swing.events()) event|=e.kind==EventKind::PointLaunch;
        check(event && launch.velocity.y>inert().jumpSpeed,"native landing did not point launch");
        check(launch.velocity.x>3,"ground contact lost the launch direction");
        check(!swing.pointLaunchReady(),"point launch reused an old zip");
        swing.releaseAll();
        check(!swing.pointLaunchReady(),"cancellation retained the launch window");
    });
    test("VR eye dimensions include high resolution and reject unbounded allocation", [] {
        check(validEyeSize(1536) && validEyeSize(4096) && validEyeSize(8192),"high resolution rejected");
        check(!validEyeSize(0) && !validEyeSize(63) && !validEyeSize(8193),"invalid eye size accepted");
    });
    test("the render scale sizes the eyes from the headset's recommendation", [] {
        using Size=std::array<uint32_t,2>;
        check(scaledEyeSize(3072,3264,100)==Size{3072,3264},"100% is not the recommendation");
        check(scaledEyeSize(2500,2690,100)==Size{2500,2690},"100% rounded an uneven recommendation");
        check(scaledEyeSize(3072,3264,150)==Size{4608,4896},"150% of the Quest 3's high preset");
        check(scaledEyeSize(2496,2688,125)==Size{3120,3360},"125% of the Quest 3's default");
        check(scaledEyeSize(2496,2688,50)==Size{1248,1344},"50%");
        check(scaledEyeSize(2500,2690,110)==Size{2752,2960},"not rounded to multiples of 8");
        // Virtual Desktop recommended 4032 x 3648 to one player: 200% would be 8064 x 7296.
        check(scaledEyeSize(4032,3648,200)==Size{8064,7296},"200% below the cap");
        const auto capped=scaledEyeSize(4320,4320,200);
        check(capped==Size{8192,8192},"200% above the cap");
        // A runtime that takes at most 5000 a side: the shape stays, within 8 pixels.
        const auto limited=scaledEyeSize(3072,3264,200,5000,5000);
        check(limited[0]<=5000 && limited[1]<=5000 && limited[1]>=4992 && limited[0]%8==0 && limited[1]%8==0 &&
              std::abs(static_cast<double>(limited[0])/limited[1]-3072./3264)<.003,"runtime limit or shape lost");
        check(scaledEyeSize(0,3264,150)==Size{0,0} && !validEyeSize(scaledEyeSize(3072,3264,150,40,40)[0]),
              "an unusable recommendation or limit gave a usable size");
        check(validRenderScale(50) && validRenderScale(100) && validRenderScale(200) && !validRenderScale(49) &&
              !validRenderScale(201) && !validRenderScale(0),"render scale range");
    });
    test("upscaling sizes the eyes' scene in multiples of 8", [] {
        // The October 10 measurement: 3072 x 3264 eyes at 67% rendered 2064 x 2192.
        check(validEyeUpscale(50) && validEyeUpscale(67) && validEyeUpscale(100) && !validEyeUpscale(49) &&
              !validEyeUpscale(101) && !validEyeUpscale(0) && validEyeUpscale(defaultEyeUpscale),"upscale range");
        check(eyeRenderSize(3072, 3264, 100) == std::array<uint32_t, 2>{3072, 3264} &&
              eyeRenderSize(3072, 3264, 67) == std::array<uint32_t, 2>{2064, 2192} &&
              eyeRenderSize(3072, 3264, 50) == std::array<uint32_t, 2>{1536, 1632} &&
              eyeRenderSize(2496, 2688, 77) == std::array<uint32_t, 2>{1928, 2072} &&
              eyeRenderSize(100, 100, 50) == std::array<uint32_t, 2>{64, 64},"upscaled scene sizes");
    });
    test("the monitor gets the left eye at its rate, whatever the game's", [] {
        check(validMonitorRate(0) && validMonitorRate(30) && validMonitorRate(240) && !validMonitorRate(241) &&
              validMonitorRate(defaultMonitorRate),"monitor rate range");
        // 0: every present. Otherwise the first one, then those on the rate's grid.
        int64_t due{};
        check(monitorPresentDue(5, due, 1000, 0) && due == 0,"every present with no rate");
        // 66 frames a second, 30 presents a second wanted (ticks of 1/66000 s): the first frame, then 30 of
        // the second after it.
        check(monitorPresentDue(1000, due, 66000, 30) && due == 2200,"the first present");
        int presents{};
        for (int frame = 1; frame <= 66; ++frame)
            presents += monitorPresentDue(1000 + frame * 1000, due, 66000, 30);
        check(presents == 30,"30 of a second's 66 frames");
        // A game slower than the rate: every frame.
        due = 0;
        presents = 0;
        for (int frame = 0; frame < 20; ++frame)
            presents += monitorPresentDue(1000 + frame * 3300, due, 66000, 30);
        check(presents == 20,"every frame of a slower game");
        // After a pause (a menu, the game screen) the next present goes out at once.
        check(monitorPresentDue(10'000'000, due, 66000, 30) && due == 10'000'000 + 2200,"at once after a pause");
    });
    test("the monitor's left eye is the one the headset showed while a copy of it is fresh", [] {
        // Half a second; two and a half pictures of a window slower than 5 a second.
        check(shownEyeHoldMs(0) == 500 && shownEyeHoldMs(240) == 500 && shownEyeHoldMs(30) == 500 &&
              shownEyeHoldMs(5) == 500 && shownEyeHoldMs(4) == 625 && shownEyeHoldMs(1) == 2500,
              "how long a copy stays");
        check(!shownEyeFresh(0, 1000, 30),"no copy yet: the game's own image");
        check(shownEyeFresh(1000, 1000, 30) && shownEyeFresh(1000, 1500, 30) &&
              !shownEyeFresh(1000, 1501, 30),"half a second without a newer copy");
        // The slowest window the launcher offers shows each copy 200 ms after the last.
        check(shownEyeFresh(1000, 1000 + 2 * 1000 / 5, 5),"a missed copy at 5 a second does not end it");
        check(shownEyeFresh(1000, 3500, 1) && !shownEyeFresh(1000, 3501, 1),"one picture a second");
        check(!shownEyeFresh(2000, 1000, 30),"a clock that went back");
    });
    test("A held on the ground charges a leap; a tap is the game's jump, made as A is let go", [] {
        using game_pad::ChargeJump;
        const float dt=1.f/90;
        // A tap: nothing while it is down, then the game's A alone for a moment.
        ChargeJump tap;
        for(int i=0;i<8;++i){const auto k=tap.update(dt,true,true);check(!k.jump&&k.leap==0,"a tap reached the game before it was let go");}
        int pulse{};
        for(int i=0;i<30;++i){const auto k=tap.update(dt,false,true);check(k.leap==0,"a tap leapt");pulse+=k.jump;}
        check(pulse>=6&&pulse<=10&&!tap.charges()&&!tap.leaps(),"a tap was not one short press of A");
        // Held: it charges from tapSeconds on, the game gets no A, and letting go leaps with the charge.
        ChargeJump held;
        int waited{};
        while(!held.charging()){const auto k=held.update(dt,true,true);check(!k.jump&&k.leap==0,"A reached the game while it waited");++waited;}
        check(waited>=15&&waited<=18&&held.charges()==1,"a held A did not charge after a fifth of a second");
        for(int i=0;i<36;++i){const auto k=held.update(dt,true,true);check(!k.jump&&k.leap==0,"the charge went to the game");}
        near(held.charge(),.5f,.03f);
        auto k=held.update(dt,false,true);
        check(!k.jump&&!held.charging()&&held.leaps()==1,"letting go did not leap");
        near(k.leap,.5f,.03f);
        // The leap is held out for the swing to see, then over; the game never got an A.
        int out{};
        for(int i=0;i<40;++i){k=held.update(dt,false,true);check(!k.jump,"a jump after the leap");out+=k.leap>0;}
        check(out>=11&&out<=15,"the leap was not held out for a moment");
        // A full charge is 1, held as long as you like; the briefest one still leaps.
        ChargeJump full;
        for(int i=0;i<200;++i)full.update(dt,true,true);
        near(full.charge(),1);
        near(full.update(dt,false,true).leap,1);
        ChargeJump brief;
        while(!brief.charging())brief.update(dt,true,true);
        near(brief.update(dt,false,true).leap,ChargeJump::leastCharge,.02f);
        // Off the ground while it charges (an edge): no leap, and nothing until A is let go.
        ChargeJump fell;
        for(int i=0;i<30;++i)fell.update(dt,true,true);
        check(fell.charging(),"no charge");
        k=fell.update(dt,true,false);
        check(!k.jump&&k.leap==0&&!fell.charging(),"a charge kept in the air");
        for(int i=0;i<10;++i){k=fell.update(dt,true,false);check(!k.jump&&k.leap==0,"a held A came back in the air");}
        check(fell.update(dt,false,true).leap==0&&!fell.leaps(),"a leap from a charge dropped in the air");
        // A press that starts off the ground (a wall, a perch) is the game's A as it is held.
        ChargeJump wall;
        k=wall.update(dt,true,false);
        check(k.jump&&k.leap==0,"a wall jump waited");
        for(int i=0;i<40;++i){k=wall.update(dt,true,true);check(k.jump&&k.leap==0,"a jump from a wall turned into a charge");}
        check(!wall.update(dt,false,true).jump&&!wall.charges(),"it stayed down");
        // Off an edge before it is decided: the press is the game's A from there.
        ChargeJump edge;
        edge.update(dt,true,true);
        k=edge.update(dt,true,false);
        check(k.jump&&k.leap==0,"a press lost at an edge");
    });
    test("a charged leap takes the body off the ground: up by its charge, and along the stick", [] {
        TestWorld w;w.enabled=false;
        const Body standing{{0,0,0},{},true};
        const auto leap=[&](float charge,Vec3 stick,Body from) {
            Swing s(inert());Input in;in.leap=charge;in.move=stick;
            const auto out=s.predictNativeStep(1.f/60,in,w,from);
            bool leapt{};
            for(const auto& e:s.events())leapt|=e.kind==EventKind::Leap;
            return std::pair{out,leapt};
        };
        const auto [low,lowLeapt]=leap(.15f,{},standing);
        check(lowLeapt,"no leap");
        near(low.velocity.y,9.9f,.01f);near(std::hypot(low.velocity.x,low.velocity.z),0);
        const auto [high,highLeapt]=leap(1,{},standing);
        check(highLeapt,"no full leap");
        near(high.velocity.y,15,.01f);
        check(high.target.y>.2f,"the leap did not ask the body up");
        const auto [far,farLeapt]=leap(1,{0,0,-1},standing);
        check(farLeapt,"no leap forward");
        near(far.velocity.z,-12,.01f);near(far.velocity.y,15,.01f);
        check(!leap(1,{},{{0,5,0},{},false}).second,"a leap in the air");
        // It counts once, however long it is held out; another needs it let go first.
        Swing s(inert());Input in;in.leap=.5f;
        s.predictNativeStep(1.f/60,in,w,standing);
        int again{};
        for(int i=0;i<10;++i){s.predictNativeStep(1.f/60,in,w,standing);for(const auto& e:s.events())again+=e.kind==EventKind::Leap;}
        check(!again,"a leap held out leapt again");
        in.leap=0;s.predictNativeStep(1.f/60,in,w,standing);
        in.leap=.5f;s.predictNativeStep(1.f/60,in,w,standing);
        for(const auto& e:s.events())again+=e.kind==EventKind::Leap;
        check(again==1,"no second leap after it was let go");
        // Its command: version 4 carries it and where the head looks; version 3 has neither.
        game_swing::Command c;c.serial=1;c.leap=.75f;c.look={0,0,-1};
        check(game_swing::valid(c)&&game_swing::input(c).leap==.75f&&game_swing::input(c).look.z==-1,"a version 4 command lost its leap");
        c.leap=2;check(!game_swing::valid(c),"a leap past full accepted");
        c.leap=0;c.look={0,3,0};check(!game_swing::valid(c),"a look that is no direction accepted");
        game_swing::Command old;old.serial=1;old.version=3;old.bytes=game_swing::commandBytesV3;
        check(game_swing::valid(old)&&game_swing::input(old).leap==0,"a version 3 command refused");
    });
    test("the game's camera follows the head's pitch as it follows its turn", [] {
        game_pad::CameraPitch pitch;
        const auto at=[](float degrees){const float r=degrees*.0174533f;return Vec3{0,std::sin(r),-std::cos(r)};};
        // A head that looks about a little moves nothing.
        check(pitch.update(at(-5),at(-10),true)==0&&pitch.turns()==0,"five degrees turned the camera");
        // Looking down at his feet: the stick down, the further the harder, until the camera is there.
        const float down=pitch.update(at(-60),at(-10),true);
        check(down<-.9f&&pitch.turns()==1,"looking down did not pitch the camera down");
        near(pitch.error(),-50,.1f);
        check(pitch.update(at(-60),at(-45),true)<down+.2f&&pitch.update(at(-60),at(-52),true)<0,"it stopped short");
        check(pitch.update(at(-60),at(-57),true)==0,"it did not rest once there");
        // Up, the other way; and no further than the camera goes.
        check(pitch.update(at(40),at(-10),true)>.9f,"looking up did not pitch the camera up");
        check(pitch.update(at(89),at(61),true)==0,"straight up drove the camera against its stop");
        check(pitch.update(at(-89),at(-79),true)==0,"straight down drove the camera against its stop");
        // Not while it is not the player's to steer.
        check(pitch.update(at(-60),at(-10),false)==0&&pitch.error()==0,"it steered a camera that is not the player's");
        game_pad::Walk walk;walk.lookUp=-1;walk.look=.5f;
        const auto pad=game_pad::fromControllers(XrFrame{},game_pad::Mapping::gameplay,walk);
        check(pad.thumbRY==-32767&&pad.thumbRX>16000,"the camera's stick is not the game's right stick");
    });
    test("a trigger pulled with its hand at the temple is the game's scan, and that trigger's alone", [] {
        using game_pad::TemplePress;
        const float dt=1.f/90;
        const Pose head{{0,1.7f,0},{}};
        const bool both[2]{true,true};
        // The right hand a hand's width out from the right temple, the left at the hip.
        Pose grips[2]{{{-.25f,1.f,-.1f},{}},{{.2f,1.72f,.02f},{}}};
        float triggers[2]{};
        TemplePress scan;
        check(!scan.update(dt,true,head,grips,both,triggers)&&!scan.hands(),"a hand at the temple alone scanned");
        triggers[1]=1;
        check(scan.update(dt,true,head,grips,both,triggers)&&scan.hands()==2&&scan.scans()==1,"no scan from the temple");
        // It holds while the trigger does, wherever the hand goes.
        grips[1].position={.3f,1.2f,-.4f};
        for(int i=0;i<30;++i)check(scan.update(dt,true,head,grips,both,triggers)&&scan.hands()==2,"the scan let go of a held trigger");
        triggers[1]=0;
        scan.update(dt,true,head,grips,both,triggers);
        check(!scan.hands(),"a released trigger is still the scan's");
        for(int i=0;i<20;++i)scan.update(dt,true,head,grips,both,triggers);
        check(!scan.held()&&scan.scans()==1,"the scan went on");
        // A quick pull still holds the game's R3 long enough to count.
        grips[1].position={.2f,1.72f,.02f};
        triggers[1]=1;scan.update(dt,true,head,grips,both,triggers);
        triggers[1]=0;
        int frames{};
        while(scan.update(dt,true,head,grips,both,triggers))++frames;
        check(frames>=11&&frames<=14&&scan.scans()==2,"a quick pull was too short for the game");
        // A trigger pulled anywhere else is a web ball as before; so is one pulled before the hand came up.
        TemplePress away;
        grips[1].position={.3f,1.3f,-.4f};triggers[1]=1;
        check(!away.update(dt,true,head,grips,both,triggers)&&!away.hands(),"a trigger at the chest scanned");
        grips[1].position={.2f,1.72f,.02f};
        check(!away.update(dt,true,head,grips,both,triggers),"a trigger already down scanned at the temple");
        // The head turned: its right is where it looks from. Either hand, either temple.
        TemplePress turned;
        const Pose looking{{0,1.7f,0},Quat::yaw(1.5708f)};
        const Vec3 right=looking.orientation.rotate({1,0,0});
        Pose hands[2]{{looking.position+right*-.2f,{}},{{.5f,1,0},{}}};
        float pulls[2]{1,0};
        check(turned.update(dt,true,looking,hands,both,pulls)&&turned.hands()==1,"the left hand at the left temple of a turned head");
        // Not outside VR play, and a trigger held through it must be let go first.
        TemplePress menu;
        grips[1].position={.2f,1.72f,.02f};triggers[1]=1;
        check(!menu.update(dt,false,head,grips,both,triggers),"a scan on the game screen");
        check(!menu.update(dt,true,head,grips,both,triggers),"a trigger held into play scanned");
        // An untracked hand scans nothing.
        TemplePress lost;
        const bool none[2]{};
        check(!lost.update(dt,true,head,grips,none,triggers),"an untracked hand scanned");
        game_pad::Walk walk;walk.scan=true;
        check(game_pad::fromControllers(XrFrame{},game_pad::Mapping::gameplay,walk).buttons==game_pad::rightThumb,
              "the scan is not the game's R3");
    });
    test("called blows: the flow calls its next blow, and the call stands until a fist answers it", [] {
        Cues cues;
        const Vec3 ahead{0,0,-1}; // the player looks down -z at him: his right is +x
        check(!cues.answers({1,0,0},ahead)&&cues.cue().way==CueWay::None,"an answer with nothing called");
        check(!cues.call(1,false,false,0)&&cues.cue().way==CueWay::None,"the flow's first blow called one");
        check(cues.call(2,false,false,0)&&cues.cue().way==CueWay::Left&&!cues.cue().finisher,"the second blow called none");
        // Any other blow leaves it standing, the same way.
        check(!cues.answers({1,0,0},ahead)&&!cues.answers(ahead,ahead),"the opposite way, or a straight jab, answered LEFT");
        check(!cues.call(3,false,false,7)&&cues.cue().way==CueWay::Left,"a blow that did not answer changed the call");
        // The count before the finisher: the same call is the finisher's now.
        check(!cues.call(4,true,false,9)&&cues.cue().way==CueWay::Left&&cues.cue().finisher,"the standing call did not become the finisher's");
        check(cues.answers({-1,0,0},ahead),"a fist to the left did not answer LEFT");
        // Answered: another way.
        check(cues.call(5,false,true,4)&&cues.cue().way!=CueWay::Left&&cues.cue().way!=CueWay::None&&!cues.cue().finisher,
              "an answered call did not give way to another");
        // Within 60 degrees of the way counts; a straight jab answers no way.
        for(const auto called:{CueWay::Left,CueWay::Right,CueWay::Up,CueWay::Down}) {
            const Vec3 wanted=Cues::direction(called,ahead);
            near(length(wanted),1);near(dot(wanted,ahead),0);
            for(unsigned chance=0;chance<64;++chance) {
                Cues one;one.call(2,false,false,chance);
                if(one.cue().way!=called)continue;
                check(one.answers(normalized(wanted+ahead*.9f),ahead),"a hook thrown forward too");
                check(!one.answers(ahead,ahead),"a straight jab answered a way");
                check(!one.answers(normalized(wanted*.4f+ahead),ahead),"barely off straight answered");
                check(!one.answers({0,0,0},ahead),"no motion answered");
                break;
            }
        }
        near(Cues::direction(CueWay::Right,ahead).x,1);
        near(Cues::direction(CueWay::Up,ahead).y,1);
        near(Cues::direction(CueWay::Left,{1,-.5f,0}).z,-1); // looking down +x and below: his left is -z
        // Every way comes up, never the one before.
        unsigned seen{};CueWay last{};Cues many;
        for(unsigned chance=0;chance<200;++chance) {
            many.call(2,false,true,chance*2654435761u>>13);
            check(many.cue().way!=last,"the same way twice running");
            last=many.cue().way;seen|=1u<<static_cast<unsigned>(last);
        }
        check(seen==0x1e,"a way that never comes up");
        // The finisher is called from the flow's first blow if need be; a flow that starts over calls nothing.
        Cues first;
        check(first.call(1,true,false,3)&&first.cue().finisher,"the finisher is not called below the flow's second blow");
        first.call(1,false,false,3);
        check(first.cue().way==CueWay::None,"a new flow kept the old call");
        first.call(2,false,false,3);first.clear();
        check(first.cue().way==CueWay::None,"a cleared call stands");
        bool refused{};
        try{CueConfig c;c.aim=1;Cues bad(c);}catch(const std::invalid_argument&){refused=true;}
        check(refused,"an aim nobody can meet accepted");
        // The flow's time, for the call's ring.
        Flow flow;
        near(flow.left(),0);
        flow.hit();near(flow.left(),1);
        flow.update(1.75f);near(flow.left(),.5f,.01f);
        flow.keep();near(flow.left(),1);
        // A thug brought to a player in the air: the player hangs longer than after a blow.
        WebStrike strike;
        strike.hangOn(false,1.6f);
        check(strike.phase()==WebStrike::Phase::Hanging,"no hang");
        float hung{};
        while(strike.step(1.f/90,{0,5,0},false,nullptr).drive&&hung<5)hung+=1.f/90;
        near(hung,1.6f,.03f);
        WebStrike ground;ground.hangOn(true,1.6f);
        check(ground.phase()==WebStrike::Phase::Idle,"a hang on the ground");
    });
    test("called blows hit harder and throw him the way they went", [] {
        const Flow flow;
        const auto blow=[](Vec3 direction,float strength) {
            PunchEvent e;e.direction=normalized(direction);e.strength=strength;e.damage=10;
            e.knockback=Knockback::Stagger;return e;
        };
        // A light hook that was called: he flies off as from a full blow.
        const auto hook=deliver(blow({1,0,-.3f},.2f),false,3,flow,{},CueWay::Right);
        check(hook.kind==BlowKind::Heavy&&hook.kinetic&&hook.fly&&hook.velocity.x>12,"a called hook did not send him flying");
        near(hook.damage,10*1.2f*1.5f,.01f);
        check(deliver(blow({1,0,-.3f},.2f),false,3,flow).kind==BlowKind::Hit,"the same blow uncalled is no jab");
        const auto up=deliver(blow({0,1,-.3f},.2f),false,3,flow,{},CueWay::Up);
        check(up.kind==BlowKind::Launch&&up.fly&&up.velocity.y>8,"a called uppercut did not lift him");
        check(deliver(blow({0,1,-.3f},.2f),true,3,flow,{},CueWay::Up).kind==BlowKind::Juggle,"called up at a thug in the air");
        const auto down=deliver(blow({0,-1,-.3f},.1f),false,3,flow,{},CueWay::Down);
        check(down.kind==BlowKind::Hit&&!down.fly&&down.knockback==Knockback::Knockdown,"a called blow down did not floor him");
        check(deliver(blow({0,-1,-.3f},.1f),true,3,flow,{},CueWay::Down).kind==BlowKind::Slam,"called down at a thug in the air");
        // The finisher is the finisher, called as it must be.
        const auto finish=deliver(blow({1,0,0},.2f),false,5,flow,{},CueWay::Right);
        check(finish.kind==BlowKind::Finisher,"a called fifth blow was no finisher");
        near(finish.damage,10*1.4f*1.5f*2.5f,.01f);
        // A blow that does not count keeps the flow going.
        Flow kept;
        for(int i=0;i<4;++i)kept.hit();
        kept.update(3);kept.keep();kept.update(3);
        check(kept.count()==4,"a blow that does not count let the flow end");
        kept.update(.6f);check(kept.count()==0,"the flow never ends");
    });
    test("moments: time slows by itself for an instant, and not for every blow of a flurry", [] {
        SlowMoments moments;
        near(moments.timeScale(),1);
        check(!moments.active()&&moments.start(Moment::blow)&&moments.active(),"no moment");
        const float dt=1.f/90;
        float lowest=1,seconds{};
        while(moments.active()&&seconds<3){moments.update(dt,true);lowest=std::min(lowest,moments.timeScale());seconds+=dt;}
        near(lowest,.3f,.005f);
        check(seconds>.65f&&seconds<.8f,"a blow's moment is not about three quarters of a second");
        near(moments.timeScale(),1);
        // The rest after it: the next blow slows nothing, a perfect dodge does.
        check(!moments.start(Moment::blow)&&!moments.start(Moment::called)&&!moments.start(Moment::airPull),
              "a flurry is one long slow motion");
        check(moments.start(Moment::dodge)&&moments.count(Moment::dodge)==1&&moments.count(Moment::blow)==1,
              "a perfect dodge waited for the rest");
        // One going is kept up for as long as the next one holds, with no second ring.
        for(int i=0;i<20;++i)moments.update(dt,true);
        const float since=moments.sinceStart();
        check(moments.start(Moment::airPull),"a moment refused during another");
        check(moments.sinceStart()==since,"a second start within one moment");
        seconds=0;
        while(moments.active()&&seconds<3){moments.update(dt,true);seconds+=dt;}
        check(seconds>1.5f&&seconds<1.65f,"the longer hold did not go on");
        // After the rest, again.
        for(int i=0;i<140;++i)moments.update(dt,true);
        check(moments.start(Moment::called),"no moment after the rest");
        // Without play it ends at once.
        for(int i=0;i<10;++i)moments.update(dt,true);
        check(moments.timeScale()<.5f,"not in yet");
        moments.update(dt,false);
        near(moments.timeScale(),1);check(!moments.active(),"a moment on the game screen");
        // The look: slow motion's own, fainter, with a ring as it starts; the meter is slow motion's.
        SlowMoments look;look.start(Moment::called);
        look.update(dt,true);
        SlowMotionView manual;
        auto view=look.view(manual);
        check(view.ripple>=0&&view.ripple<.1f&&view.entering&&!view.active&&view.meter==0,"no ring as a moment starts");
        for(int i=0;i<12;++i)look.update(dt,true);
        view=look.view(manual);
        near(view.blend,.6f,.01f);
        manual.blend=.9f;manual.ripple=.5f;
        view=look.view(manual);
        near(view.blend,.9f);near(view.ripple,.5f);
        // A knockout's is a light one: time eases to about half for a moment, with a fainter look and no ring.
        SlowMoments light;
        check(light.start(Moment::knockout)&&light.count(Moment::knockout)==1,"no moment for a knockout");
        lowest=1;seconds=0;
        while(light.active()&&seconds<3){
            light.update(dt,true);lowest=std::min(lowest,light.timeScale());seconds+=dt;
            check(light.view(SlowMotionView{}).ripple<0,"a ring for a light moment");
        }
        near(lowest,.55f,.01f);
        check(seconds>.5f&&seconds<.72f,"a knockout's moment is not about six tenths of a second");
        // A deeper one that begins in it takes over.
        SlowMoments deeper;deeper.start(Moment::knockout);
        for(int i=0;i<12;++i)deeper.update(dt,true);
        near(deeper.timeScale(),.55f,.01f);near(deeper.view(SlowMotionView{}).blend,.3f,.02f);
        check(deeper.start(Moment::blow),"a blow's moment refused during a knockout's");
        deeper.update(dt,true);near(deeper.timeScale(),.3f,.01f);
    });
    test("a knockout is lettered where he stood: KO! in yellow on a red burst, in and gone on twos", [] {
        const Vec3 viewer{0,1.6f,0},at{0,1.3f,-1.2f};
        const float pose=1.f/12;
        const auto draw=[&](int i) {
            std::vector<Vertex> out;
            appendKnockout(out,{at,pose*(static_cast<float>(i)+.5f)},viewer,.0005f);
            return out;
        };
        const Vec3 ink{.0033f,.0030f,.0070f},paper{.9301f,.9131f,.8550f},cyan{.0060f,.6724f,1},magenta{1,.0273f,.2918f},
            yellow{1,.7529f,.0742f},red{.7682f,.0194f,.0284f};
        const auto count=[](const std::vector<Vertex>& v,Vec3 plate) {
            size_t n{};
            for(const auto& x:v)n+=length(x.color-plate)<.01f;
            return n;
        };
        const auto widest=[&](const std::vector<Vertex>& v) {
            float r{};
            for(const auto& x:v)r=std::max(r,length(x.position-at));
            return r;
        };
        const auto same=[](const std::vector<Vertex>& a,const std::vector<Vertex>& b) {
            if(a.size()!=b.size())return false;
            for(size_t i=0;i<a.size();++i)
                if(length(a[i].position-b[i].position)>0||length(a[i].color-b[i].color)>0||a[i].alpha!=b[i].alpha)return false;
            return true;
        };
        const auto held=draw(4);
        check(!held.empty()&&held.size()%3==0,"no knockout's mark");
        check(count(held,yellow)>0&&count(held,red)>0&&count(held,paper)>0&&count(held,ink)>0,"not yellow lettering on a red burst with a paper heart, keyed in ink");
        const Vec3 facing=normalized(viewer-at);
        for(const auto& v:held)check(finite(v.position)&&v.alpha>=0&&v.alpha<=1&&std::abs(dot(v.position-at,facing))<1e-3f,"a vertex off its plane");
        // No wider than a chest an arm and a half away.
        check(widest(held)>.08f&&widest(held)<.25f,"the mark is tiny, or fills the view");
        check(count(held,cyan)+count(held,magenta)==0&&same(draw(3),draw(5)),"it does not hold");
        check(widest(draw(0))<widest(held)*.5f&&widest(draw(1))>widest(held)*1.05f&&count(draw(1),magenta)>0,"it does not pop in");
        check(widest(draw(6))>widest(held)*1.05f&&count(draw(6),cyan)>0&&widest(draw(7))<widest(held)*.7f,"it does not leave in two poses");
        check(draw(8).empty(),"it stayed");
        std::vector<Vertex> none;
        appendKnockout(none,{at,-1},viewer,.0005f);
        appendKnockout(none,{{std::numeric_limits<float>::quiet_NaN(),0,0},.1f},viewer,.0005f);
        appendKnockout(none,{viewer,.1f},viewer,.0005f);
        check(none.empty(),"a mark before its time, nowhere, or in the viewer's eye");
    });
    test("spider-sense asks what the game's flags allow: jump, a counter within a strike's reach, else a dodge", [] {
        using namespace attack;
        check(answerOf(dodgeable|counterable,3)==SenseAnswer::Counter,"a counterable punch is not countered");
        check(answerOf(dodgeable|counterable,10.5f)==SenseAnswer::Dodge,"a counter beyond a strike's reach");
        check(answerOf(dodgeable,2)==SenseAnswer::Dodge,"an attack no dodge interrupts asks for a counter");
        check(answerOf(dodgeable|counterable|jumpButton,2)==SenseAnswer::Jump,"the jump's attack asks for no jump");
        check(answerOf(counterable,std::numeric_limits<float>::quiet_NaN())==SenseAnswer::Dodge,"a counter nowhere");
        // A warning stands until a tenth of a second after its blow; one with no time set for 1.2 s.
        SenseThreat t{1,{},{},.3f,.2f,true,counterable};
        check(standing(t),"a warning before its blow does not stand");
        near(senseLeft(t),.625f);near(senseLeft(t,0),.6f,1e-4f);
        t.toImpact=-.05f;check(standing(t)&&senseLeft(t)==0,"a blow on its way does not stand at none left");
        t.toImpact=-.2f;check(!standing(t),"a warning long past its blow stands");
        SenseThreat untimed{1,{},{},0,1,false,counterable};
        check(standing(untimed)&&senseLeft(untimed)==1,"an untimed warning");
        untimed.warned=1.3f;check(!standing(untimed),"an untimed warning that never ends");
        check(!standing(SenseThreat{0,{},{},.3f,0,true,0}),"nobody's warning stands");
        bool refused{};
        try{SpiderSenseConfig c;c.aim=1;SpiderSense bad(c);}catch(const std::invalid_argument&){refused=true;}
        check(refused,"a counter no fist can aim");
    });
    test("spider-sense counters a fist thrown at a warned attacker once, and his next blow is the counter's", [] {
        const float dt=1.f/90;
        const Vec3 head{0,1.65f,0},chest{0,1.2f,0};
        const std::vector<SenseThreat> warned{{7,{0,1.75f,-3},{0,1,-3},.4f,.1f,true,attack::dodgeable|attack::counterable},
                                              {8,{3,1.75f,-3},{3,1,-3},.4f,.1f,true,attack::dodgeable}};
        // The right fist thrown from the shoulder at the first attacker's middle at 3.5 m/s.
        const auto throwAt=[&](Vec3 at,float speed,Vec3 from={.2f,1.4f,-.35f}) {
            std::array<SenseFist,2> fists{};
            fists[1]={true,from,normalized(at-from)*speed};
            return fists;
        };
        SpiderSense sense;
        std::optional<SpiderSense::Counter> made;
        int samples{};
        for(;samples<10&&!made;++samples)made=sense.update(dt,warned,throwAt({0,1,-3},3.5f),head,chest);
        check(made&&made->attacker==7&&made->hand==1,"a fist thrown at him is no counter");
        check(samples>=3&&samples<=4,"a counter before the fist's sustain, or long after it");
        near(made->left,.4f);check(!made->perfect,"a counter 0.4 s early is a perfect one");
        check(sense.countered(7)&&!sense.countered(8),"the countered attacker");
        // The same throw counts once.
        for(int i=0;i<20;++i)check(!sense.update(dt,warned,throwAt({0,1,-3},3.5f),head,chest),"one throw, two counters");
        // His next blow is the counter's, once.
        std::optional<SpiderSense::Counter> again;
        check(sense.blow(7,warned,chest,again)&&!again,"the blow after a counter is not its blow");
        check(!sense.blow(7,warned,chest,again),"a counter with two blows");
        // A dodge's attacker, a slow fist, one thrown away, a hand drawn back: none.
        SpiderSense other;
        for(int i=0;i<20;++i) {
            check(!other.update(dt,warned,throwAt({3,1,-3},3.5f),head,chest),"a dodge's attacker countered");
            check(!other.update(dt,warned,throwAt({0,1,-3},1.5f),head,chest),"a slow fist countered");
            check(!other.update(dt,warned,throwAt({0,1,3},3.5f),head,chest),"a fist thrown away countered");
            check(!other.update(dt,warned,throwAt({0,1,-3},3.5f,{.1f,1.5f,-.1f}),head,chest),"a hand at the chest");
        }
        // The last fifth of a second: a perfect counter.
        SpiderSense late;
        const std::vector<SenseThreat> soon{{7,{0,1.75f,-3},{0,1,-3},.15f,.5f,true,attack::counterable}};
        std::optional<SpiderSense::Counter> perfect;
        for(int i=0;i<10&&!perfect;++i)perfect=late.update(dt,soon,throwAt({0,1,-3},3),head,chest);
        check(perfect&&perfect->perfect,"a counter in the warning's last fifth of a second is not perfect");
        // A punch that lands while his warning stands counters him itself: its blow is the counter's.
        SpiderSense landing;
        landing.update(dt,warned,{},head,chest);
        std::optional<SpiderSense::Counter> byBlow;
        check(landing.blow(7,warned,chest,byBlow)&&byBlow&&byBlow->attacker==7&&byBlow->hand==-1,"a blow in his warning");
        check(landing.countered(7)&&!landing.blow(7,warned,chest,byBlow),"a landed counter with a second blow");
        // Nor the dodge's attacker, nor anyone after his warning: an ordinary blow.
        check(!landing.blow(8,warned,chest,byBlow)&&!byBlow,"a blow at a dodge's attacker counters him");
        std::vector<SenseThreat> past=warned;past[0].toImpact=-.5f;
        SpiderSense after;
        check(!after.blow(7,past,chest,byBlow),"a blow long after his attack counters it");
        // A counter's blow waits 1.2 s at most; a warning gone and no blow coming is forgotten.
        SpiderSense waits;
        for(int i=0;i<10;++i)waits.update(dt,warned,throwAt({0,1,-3},3.5f),head,chest);
        for(int i=0;i<120;++i)waits.update(dt,{},{},head,chest);
        check(!waits.blow(7,{},chest,byBlow)&&!waits.countered(7),"a counter's blow two seconds on");
    });
    test("spider-sense's sightline: an attacker over 31 degrees off the look, kept to 26, on his side, behind too", [] {
        const Pose head{{0,1.6f,0},{}};
        const auto at=[&](float degrees,float up=0) {
            const float r=degrees*3.14159265f/180;
            return head.position+Vec3{std::sin(r),up,-std::cos(r)}*5.f;
        };
        SenseSight s=senseSight(head,at(10),{});
        check(!s.shown,"an attacker in front shown in the sightline");
        s=senseSight(head,at(40),s);
        check(s.shown&&s.x>.99f&&std::abs(s.y)<.02f,"an attacker to the right is not at the sightline's right");
        s=senseSight(head,at(28),s);check(s.shown,"the sightline's mark lets go too soon");
        s=senseSight(head,at(20),s);check(!s.shown,"the sightline's mark stays");
        s=senseSight(head,at(28),s);check(!s.shown,"the sightline's mark comes back too soon");
        SenseSight left=senseSight(head,at(-70,.4f),{});
        check(left.shown&&left.x<-.8f&&left.y>.2f,"an attacker up to the left");
        // Behind: the side he was last on.
        SenseSight behind=senseSight(head,at(-120),{});
        behind=senseSight(head,at(180),behind);
        check(behind.shown&&behind.x<-.99f,"behind, the mark changed sides");
        SenseSight fresh=senseSight(head,at(179.5f),{});
        check(fresh.shown&&fresh.x>.99f,"a new attacker straight behind, a little to the right");
        check(!senseSight(head,head.position,{}).shown,"an attacker in the head");
    });
    test("spider-sense's mark is printed: the comic's lines in what to do, a ring closing on the blow, broken by a counter", [] {
        const Vec3 viewer{0,1.6f,0},headTop{0,1.75f,-3};
        const float pose=1.f/12;
        const auto draw=[&](SenseAnswer a,float left,float age=.52f,float broken=-1,bool timed=true) {
            std::vector<Vertex> out;
            appendSense(out,{headTop,a,left,timed,age,broken},viewer,.0005f);
            return out;
        };
        const Vec3 ink{.0033f,.0030f,.0070f},paper{.9301f,.9131f,.8550f},cyan{.0060f,.6724f,1},magenta{1,.0273f,.2918f},
            yellow{1,.7529f,.0742f},red{.7682f,.0194f,.0284f};
        const auto count=[](const std::vector<Vertex>& v,Vec3 plate) {
            size_t n{};
            for(const auto& x:v)n+=length(x.color-plate)<.01f;
            return n;
        };
        // Its centre: clear over his head.
        const auto centre=[&](const std::vector<Vertex>& v) {
            Vec3 c{};
            for(const auto& x:v)c+=x.position;
            return c/static_cast<float>(std::max<size_t>(v.size(),1));
        };
        const auto widest=[&](const std::vector<Vertex>& v,Vec3 from) {
            float r{};
            for(const auto& x:v)r=std::max(r,length(x.position-from));
            return r;
        };
        for(const auto& mark:{draw(SenseAnswer::Counter,1),draw(SenseAnswer::Dodge,.5f),draw(SenseAnswer::Jump,.125f),
                              draw(SenseAnswer::Counter,1,.01f),draw(SenseAnswer::Counter,1,.5f,.1f)}) {
            check(!mark.empty()&&mark.size()%3==0&&count(mark,ink)>0,"a mark without its keyline");
            for(const auto& x:mark) {
                check(finite(x.position)&&x.alpha>=0&&x.alpha<=1,"a bad vertex");
                bool plate{};
                for(const Vec3 c:{ink,paper,cyan,magenta,yellow,red})plate|=length(x.color-c)<.01f;
                check(plate,"a colour that is none of the plates");
            }
        }
        // What it asks for is its colour; the jump's chevron is paper.
        check(count(draw(SenseAnswer::Counter,.5f),cyan)>0&&count(draw(SenseAnswer::Counter,.5f),red)==0,"a counter not cyan");
        check(count(draw(SenseAnswer::Dodge,.5f),red)>0&&count(draw(SenseAnswer::Dodge,.5f),cyan)==0,"a dodge not red");
        check(count(draw(SenseAnswer::Jump,.5f),yellow)>0,"a jump not yellow");
        check(count(draw(SenseAnswer::Jump,.5f,.52f,-1,false),paper)>count(draw(SenseAnswer::Dodge,.5f,.52f,-1,false),paper),
              "the jump's chevron");
        const Vec3 middle=centre(draw(SenseAnswer::Counter,1,.52f,-1,false));
        check(middle.y>headTop.y+.1f&&middle.y<headTop.y+.4f&&std::abs(middle.x)<.05f,"the mark is not over his head");
        // The ring closes on the blow: wide with the warning's time all there, the lines alone at its end; its
        // last notch a yellow burst's edge.
        const float lines=widest(draw(SenseAnswer::Counter,1,.52f,-1,false),middle);
        const float full=widest(draw(SenseAnswer::Counter,1),middle),half=widest(draw(SenseAnswer::Counter,.5f),middle);
        check(full>lines*1.5f&&half<full*.9f&&half>lines,"the ring does not close on the blow");
        check(count(draw(SenseAnswer::Counter,.125f),yellow)>0&&count(draw(SenseAnswer::Counter,.25f),yellow)==0,
              "the last notch is not now");
        // It pops in out of register, and leaves broken in three poses.
        check(count(draw(SenseAnswer::Counter,1,pose*.5f),magenta)>0&&count(draw(SenseAnswer::Counter,1),magenta)==0,
              "it does not land out of register");
        check(count(draw(SenseAnswer::Counter,1,.5f,pose*.5f),magenta)>0,"a counter's break has no flash");
        check(count(draw(SenseAnswer::Counter,1,.5f,pose*1.5f),cyan)>0&&!draw(SenseAnswer::Counter,1,.5f,pose*2.5f).empty(),
              "the break's poses");
        check(draw(SenseAnswer::Counter,1,.5f,pose*3.5f).empty(),"a broken mark stays");
        // A few degrees across at any distance.
        for(const float d:{1.5f,4.f,9.f}) {
            std::vector<Vertex> out;
            appendSense(out,{viewer+Vec3{0,.1f,-d},SenseAnswer::Counter,1,false,.52f,-1},viewer,.0005f);
            const Vec3 c=centre(out);
            const float degrees=2*std::atan(widest(out,c)/length(c-viewer))*180/3.14159265f;
            check(degrees>4&&degrees<12,"the mark is too small or too big to read");
        }
        std::vector<Vertex> none;
        appendSense(none,{headTop,SenseAnswer::None,1,true,.5f,-1},viewer,.0005f);
        appendSense(none,{headTop,SenseAnswer::Counter,1,true,-.1f,-1},viewer,.0005f);
        appendSense(none,{{std::numeric_limits<float>::quiet_NaN(),0,0},SenseAnswer::Counter,1,true,.5f,-1},viewer,.0005f);
        appendSense(none,{viewer,SenseAnswer::Counter,1,true,.5f,-1},viewer,.0005f);
        check(none.empty(),"a mark for nothing, before its time, nowhere, or in the viewer's eye");
    });
    test("spider-sense in the sightline: the mark at 21 degrees on his side with an arrowhead, lines at the rim", [] {
        const Pose head{{0,1.6f,0},{}};
        const Vec3 forward{0,0,-1};
        const auto draw=[&](float x,float y,bool shown=true,float broken=-1) {
            std::vector<Vertex> out;
            SenseSightView view;
            view.answer=SenseAnswer::Dodge;
            view.sight={shown,x,y};
            view.timed=true;
            view.left=.5f;
            view.age=.52f;
            view.broken=broken;
            appendSenseSight(out,view,head,.001f);
            return out;
        };
        const Vec3 red{.7682f,.0194f,.0284f};
        const auto off=[&](const Vertex& v){return std::acos(std::clamp(dot(normalized(v.position-head.position),forward),-1.f,1.f))*180/3.14159265f;};
        const auto right=draw(1,0);
        check(!right.empty(),"no sightline mark");
        float least=180,most=0;
        size_t reds{};
        for(const auto& v:right) {
            check(finite(v.position)&&v.position.x>0,"a sightline mark not to the right");
            least=std::min(least,off(v));most=std::max(most,off(v));
            near(length(v.position-head.position),1.f,.08f);
            reds+=length(v.color-red)<.01f;
        }
        check(least>14&&least<21&&most>38&&most<45,"the mark and the rim's lines are not where the sightline is");
        check(reds>0,"the dodge's red is missing");
        bool up{};
        for(const auto& v:draw(0,1))up|=v.position.y>head.position.y+.2f;
        check(up,"an attacker above is not at the top of the view");
        check(draw(1,0,false).empty()&&draw(0,0).empty(),"a sightline mark not shown, or nowhere");
        // Broken: the mark's poses, no arrowhead or rim lines.
        float brokenMost{};
        for(const auto& v:draw(1,0,true,.01f))brokenMost=std::max(brokenMost,off(v));
        check(brokenMost>0&&brokenMost<30,"a broken sightline mark kept its rim lines");
    });
    test("one enemy at a time: a look marks, a blow locks until he is down, and a square look at another moves it", [] {
        const Vec3 head{0,1.65f,0};
        const float dt=1.f/90;
        const std::vector<FightTarget> thugs{{1,{0,0,-4},1.8f,.3f,false},{2,{3,0,-4},1.8f,.3f,false}};
        const auto at=[&](Vec3 where){return normalized(where-head);};
        const Vec3 first=at({0,.9f,-4}),second=at({3,.9f,-4});
        // A look marks the enemy nearest the line of sight, and the mark goes with the look.
        Lock lock;
        check(lock.update(dt,head,first,thugs)==1&&!lock.locked(),"a look does not mark");
        check(lock.update(dt,head,second,thugs)==2,"the mark does not follow the look");
        check(lock.update(dt,head,{0,0,1},thugs)==0,"an enemy behind the player is marked");
        // A blow locks: a glance at another takes nothing.
        lock.update(dt,head,first,thugs);
        lock.take(1);
        check(lock.locked()&&lock.target()==1,"a blow does not lock");
        for(int i=0;i<20;++i)lock.update(dt,head,second,thugs);
        check(lock.locked()&&lock.target()==1,"a glance took the lock");
        // Nor does looking away from everyone.
        for(int i=0;i<90;++i)lock.update(dt,head,{0,0,1},thugs);
        check(lock.locked()&&lock.target()==1,"looking away lost the lock");
        // Looked at squarely for a third of a second, with the locked one well aside, the other takes it: a look's mark.
        for(int i=0;i<30;++i)lock.update(dt,head,second,thugs);
        check(lock.target()==2&&!lock.locked(),"a square look at another does not move the lock");
        // Two thugs side by side in the view: looking from one to the other never moves a lock.
        const std::vector<FightTarget> pair{{1,{-.5f,0,-4},1.8f,.3f,false},{2,{.5f,0,-4},1.8f,.3f,false}};
        Lock close;
        close.update(dt,head,at({-.5f,.9f,-4}),pair);
        close.take(1);
        for(int i=0;i<180;++i)close.update(dt,head,at({.5f,.9f,-4}),pair);
        check(close.locked()&&close.target()==1,"the lock went to the thug beside him");
        // He is down: gone from the fight. The mark is a look's again at once.
        const std::vector<FightTarget> left{pair[1]};
        check(close.update(dt,head,at({.5f,.9f,-4}),left)==2&&!close.locked(),"a lock on a thug who is down");
        // Left alone for four seconds, a lock is a look's mark again.
        Lock idle;
        idle.update(dt,head,first,thugs);
        idle.take(1);
        for(int i=0;i<340;++i)idle.update(dt,head,first,thugs);
        check(idle.locked(),"a lock let go early");
        for(int i=0;i<30;++i)idle.update(dt,head,first,thugs);
        check(!idle.locked()&&idle.target()==1,"a lock that is never let go");
        // A blow at him again starts the four seconds anew; nobody is no one to lock.
        idle.take(1);idle.take(0);
        check(idle.locked()&&idle.target()==1,"taking nobody changed the lock");
        idle.clear();
        check(!idle.locked()&&idle.target()==0,"a cleared lock");
        // One beyond a strike's reach is no look's mark, but a lock on him holds.
        const std::vector<FightTarget> far{{5,{0,0,-12},1.8f,.3f,false}};
        Lock reach;
        check(reach.update(dt,head,at({0,.9f,-12}),far)==0,"an enemy beyond reach is marked");
        reach.take(5);
        check(reach.update(dt,head,at({0,.9f,-12}),far)==5&&reach.locked(),"a lock let go of a thug knocked out of reach");
    });
    test("the mark over a thug is printed: a paper ring, a call's arrow pointing its way over a ring that runs down, the finisher's yellow in a red burst", [] {
        const Vec3 viewer{0,1.6f,0},point{0,1.4f,-1.5f};
        const float pose=1.f/12,size=.1f*length(viewer-point)/.9f;
        const auto draw=[&](CueWay way,float left,bool finisher=false,float age=.52f,bool locked=false) {
            std::vector<Vertex> out;
            appendCue(out,{point,way,finisher,left,age,locked},viewer,.0005f);
            return out;
        };
        // The style's plates, linear.
        const Vec3 ink{.0033f,.0030f,.0070f},paper{.9301f,.9131f,.8550f},cyan{.0060f,.6724f,1},magenta{1,.0273f,.2918f},
            yellow{1,.7529f,.0742f},red{.7682f,.0194f,.0284f};
        const auto count=[](const std::vector<Vertex>& v,Vec3 plate) {
            size_t n{};
            for(const auto& x:v)n+=length(x.color-plate)<.01f;
            return n;
        };
        const auto widest=[&](const std::vector<Vertex>& v) {
            float r{};
            for(const auto& x:v)r=std::max(r,length(x.position-point));
            return r;
        };
        const auto same=[](const std::vector<Vertex>& a,const std::vector<Vertex>& b) {
            if(a.size()!=b.size())return false;
            for(size_t i=0;i<a.size();++i)
                if(length(a[i].position-b[i].position)>0||length(a[i].color-b[i].color)>0||a[i].alpha!=b[i].alpha)return false;
            return true;
        };
        // Nothing but the plates, whole but for an edge's last pixel, and every mark keyed in ink.
        for(const auto& mark:{draw(CueWay::None,1),draw(CueWay::None,1,false,.52f,true),draw(CueWay::Left,.7f),draw(CueWay::Up,.4f,true),
                              draw(CueWay::Right,1,false,.01f)}) {
            check(!mark.empty()&&mark.size()%3==0&&count(mark,ink)>0,"a mark without its keyline");
            for(const auto& x:mark) {
                check(finite(x.position)&&x.alpha>=0&&x.alpha<=1,"a bad vertex");
                bool plate{};
                for(const Vec3 c:{ink,paper,cyan,magenta,yellow,red})plate|=length(x.color-c)<.01f;
                check(plate,"a colour that is none of the plates");
            }
        }
        // The arrow is a call's paper. It comes in from outside the ring on the side the fist comes from (its far
        // end), and its barbs, all of it far to either side of its way, are at its other end, by his middle.
        const auto ahead=[&](CueWay way,bool level) {
            float far{},barbs{},m{};
            for(const auto& x:draw(way,1)) {
                if(length(x.color-paper)>=.01f||x.alpha<.9f)continue;
                const Vec3 p=x.position-point;
                const float along=level?p.x:p.y,across=level?p.y:p.x;
                if(std::abs(along)>std::abs(far))far=along;
                if(std::abs(across)>=size*.25f){barbs+=along;m+=1;}
            }
            check(m>0,"no arrow");
            check(std::abs(far)>size*1.2f,"the arrow does not come in from outside the ring");
            check(std::abs(barbs/m)<size*.35f,"the arrow's head is not by his middle");
            // The way it points: from its far end to its barbs, more than the ring's radius apart.
            return barbs/m-far>size?1:far-barbs/m>size?-1:0;
        };
        check(ahead(CueWay::Right,true)==1&&ahead(CueWay::Left,true)==-1,"left and right point the wrong way");
        check(ahead(CueWay::Up,false)==1&&ahead(CueWay::Down,false)==-1,"up and down point the wrong way");
        // The ring: less of it as the time runs out, a notch at a time.
        check(count(draw(CueWay::Up,1),cyan)>count(draw(CueWay::Up,.5f),cyan)&&count(draw(CueWay::Up,.5f),cyan)>count(draw(CueWay::Up,.2f),cyan),
              "the ring does not run down");
        check(same(draw(CueWay::Up,.5f),draw(CueWay::Up,.49f))&&!same(draw(CueWay::Up,.5f),draw(CueWay::Up,.47f)),"the ring does not run down in notches");
        // How far a plate's ring lies from the middle, nearest and farthest, above him (an arrow that points up
        // comes in from below, and its drop is the same plate).
        const auto band=[&](const std::vector<Vertex>& v,Vec3 plate,float radius) {
            float least=1e9f,most{};
            for(const auto& x:v) {
                const float r=length(x.position-point)/radius;
                if(length(x.color-plate)>=.01f||x.alpha<.9f||r<.8f||x.position.y-point.y<radius*.3f)continue;
                least=std::min(least,r);most=std::max(most,r);
            }
            return most-least;
        };
        // The finisher's call: larger, a yellow arrow, and its ring a burst's edge, in and out where a call's is round.
        const auto finisher=draw(CueWay::Up,1,true);
        check(count(finisher,yellow)>0&&count(finisher,red)>0&&count(finisher,cyan)==0&&count(finisher,paper)==0,"the finisher's call is not yellow in red");
        check(widest(finisher)>widest(draw(CueWay::Up,1))*1.2f,"the finisher's call is no larger");
        check(band(finisher,red,size*1.3f)>.2f&&band(draw(CueWay::Up,1),cyan,size)<.15f,"the finisher's ring is no burst");
        check(count(finisher,red)>count(draw(CueWay::Up,.5f,true),red)&&count(draw(CueWay::Up,.5f,true),red)>count(draw(CueWay::Up,.2f,true),red),
              "the finisher's ring does not run down");
        // A call lands on twos: a pose small, a pose past its size, both out of register; a pose short; then it holds.
        const auto rest=draw(CueWay::Right,1);
        const auto land=[&](int i){return draw(CueWay::Right,1,false,pose*(static_cast<float>(i)+.5f));};
        check(widest(land(0))<widest(rest)*.4f&&widest(land(1))>widest(rest)*1.1f&&widest(land(2))<widest(rest),"a call does not pop in");
        check(count(land(0),magenta)>0&&count(land(1),magenta)>0&&count(land(2),magenta)==0&&count(rest,magenta)==0,"a call's misprint on the wrong poses");
        check(same(draw(CueWay::Right,1,false,.26f),draw(CueWay::Right,1,false,.32f)),"a pose is not held");
        // Held, the arrow boils (the next place two poses later, a little way off) and the ring keeps still.
        const auto middle=[&](const std::vector<Vertex>& v) {
            Vec3 sum{};float n{};
            for(const auto& x:v)if(length(x.color-paper)<.01f&&x.alpha>.9f){sum+=x.position;n+=1;}
            return sum/std::max(n,1.f);
        };
        // (To his right: the arrow, with its drop, comes in from his left.)
        const auto ring=[&](const std::vector<Vertex>& v) {
            std::vector<Vertex> r;
            for(const auto& x:v)if(length(x.color-cyan)<.01f&&length(x.position-point)>size*.8f&&x.position.x-point.x>size*.3f)r.push_back(x);
            return r;
        };
        const auto a=land(6),b=land(7),c=land(8);
        check(same(a,b),"a held place changed within its two poses");
        const float moved=length(middle(c)-middle(a));
        check(moved>size*.01f&&moved<size*.06f,"the arrow does not boil, or jumps");
        check(!ring(a).empty()&&same(ring(a),ring(c)),"the ring does not keep still");
        // Nothing called: a thin paper ring over him, a target and no more; it snaps on a pose wide.
        const auto plain=draw(CueWay::None,1);
        check(count(plain,paper)>0&&count(plain,cyan)+count(plain,magenta)+count(plain,yellow)+count(plain,red)==0,"the plain ring is not paper alone");
        check(widest(plain)>size&&widest(plain)<size*1.1f,"the plain ring is not a hand wide");
        for(const auto& x:plain)check(length(x.position-point)>size*.6f,"the plain ring covers him");
        check(widest(draw(CueWay::None,1,false,.01f))>widest(plain)*1.1f&&widest(draw(CueWay::None,1,false,pose*1.5f))<widest(plain),"the plain ring does not snap on");
        check(draw(CueWay::Up,0).empty()&&draw(CueWay::Up,1,false,-1).empty(),"a mark with nothing to show");
        // Locked onto him by a blow, the ring is bolder and has four ticks in from it; it snaps on as the lock does.
        const auto locked=draw(CueWay::None,1,false,.52f,true);
        float inner=1e9f;
        for(const auto& x:locked)inner=std::min(inner,length(x.position-point));
        check(count(locked,paper)>count(plain,paper)&&inner<size*.75f&&inner>size*.6f&&widest(locked)<size*1.1f,"a lock's ring has no ticks");
        check(widest(draw(CueWay::None,1,false,.01f,true))>widest(locked)*1.1f,"a lock's ring does not snap on");
        // A called blow's burst is out of register: a cyan ghost and a magenta one.
        std::vector<Vertex> called,ordinary;
        appendImpact(called,{point,.5f,BlowKind::Heavy,1,.05f,true},viewer,.0005f);
        appendImpact(ordinary,{point,.5f,BlowKind::Heavy,1,.05f,false},viewer,.0005f);
        check(count(called,cyan)>0&&count(called,magenta)>0&&count(ordinary,cyan)+count(ordinary,magenta)==0,"a called blow's burst is not out of register");
    });
    test("flat screen command uses the native camera without requiring tracked eyes", [] {
        native_eyes::Command command;
        command.serial=1;command.enabled=2;
        check(native_eyes::valid(command),"native flat camera command rejected");
        command.enabled=4;check(!native_eyes::valid(command),"unknown camera mode accepted");
        command.enabled=2;command.leaseMs=0;
        check(!native_eyes::valid(command),"flat camera without a lease accepted");
    });
    test("a cutscene's stereo command carries the viewer's eyes and screen, not tracked poses", [] {
        native_eyes::Command command;
        command.serial=1;command.enabled=3;
        check(!native_eyes::valid(command),"a stereo screen without its numbers accepted");
        // Eyes 64 mm apart, the medium screen (3.2 m wide, 2.5 m away), medium depth.
        const auto stereo=native_eyes::screenStereo(.064f,3.2f,2.5f,.7f);
        near(stereo.convergence,1.75f);near(stereo.parallax,.02f);near(stereo.separation,.064f);
        native_eyes::setStereo(command,stereo);
        check(native_eyes::valid(command),"a stereo screen command rejected");
        const auto back=native_eyes::stereo(command);
        check(back.convergence==stereo.convergence && back.parallax==stereo.parallax &&
              back.separation==stereo.separation,"the command lost the stereo numbers");
        auto bad=command;bad.eyes[0].fov[0]=std::numeric_limits<float>::quiet_NaN();
        check(!native_eyes::valid(bad),"an unreadable convergence accepted");
        bad=command;bad.eyes[0].fov[1]=0;check(!native_eyes::valid(bad),"no parallax accepted");
        bad=command;bad.eyes[0].fov[1]=.2f;check(!native_eyes::valid(bad),"eyes a fifth of the screen apart accepted");
        bad=command;bad.eyes[0].fov[2]=.5f;check(!native_eyes::valid(bad),"cameras half a metre apart accepted");
        bad=command;bad.eyes[0].fov[0]=.01f;check(!native_eyes::valid(bad),"a convergence at the lens accepted");
        bad=command;bad.leaseMs=0;check(!native_eyes::valid(bad),"a stereo screen without a lease accepted");
    });
    test("a cutscene's two cameras: the horizon an eye distance apart, the convergence on the screen", [] {
        const auto stereo=native_eyes::screenStereo(.064f,3.2f,2.5f,1);
        // Where a point `depth` metres ahead of the middle camera lands in an eye's image (0 to 1 across),
        // the eye `side` metres to the right with its lens moved by `shift`.
        auto across=[](native_eyes::Bounds lens,float side,float shift,float depth) {
            return (-side/depth-(lens.left+shift))/(lens.right-lens.left);
        };
        // What the viewer sees: the two image points `parallax` metres apart on a screen 2.5 m away.
        auto shown=[](float parallax) { return 2.5f*.064f/(.064f-parallax); };
        for(const float half:{.2f,.35f,.64f}) {
            const native_eyes::Bounds lens{-half,half,-half*9/16,half*9/16};
            const auto eyes=native_eyes::stereoEyes(stereo,lens);
            near(eyes.shift,half*.02f,1e-5f);
            check(eyes.separation>0 && eyes.separation<=.064f+1e-6f,"cameras further apart than the viewer's eyes");
            auto parallax=[&](float depth) {
                return (across(lens,eyes.separation/2,-eyes.shift,depth)-across(lens,-eyes.separation/2,eyes.shift,depth))*3.2f;
            };
            // Infinitely far away: the viewer's eyes look at it in parallel.
            near(parallax(1e7f),.064f,1e-4f);
            // At the convergence both images agree: it lies on the screen.
            near(parallax(eyes.distance),0,1e-5f);
            near(eyes.distance,2.5f,1e-3f);
            // High depth: everything as far from the viewer as it is from the camera.
            near(shown(parallax(1)),1,2e-3f);
            near(shown(parallax(10)),10,.05f);
            // A narrow lens shows its scene larger than life: the cameras closer together, never further.
            near(eyes.separation,2*half*2.5f*.064f/3.2f,1e-5f);
        }
        // A lens wider than the screen is from the viewer: the cameras stay an eye distance apart
        // (the scene never smaller than life) and the screen's plane comes nearer.
        const native_eyes::Bounds wide{-1,1,-.5625f,.5625f};
        const auto eyes=native_eyes::stereoEyes(stereo,wide);
        near(eyes.separation,.064f,1e-6f);near(eyes.shift,.02f,1e-6f);near(eyes.distance,1.6f,1e-4f);
        // Medium depth: the screen's plane at 1.75 m, everything a little farther than it is.
        const auto medium=native_eyes::stereoEyes(native_eyes::screenStereo(.064f,3.2f,2.5f,.7f),{-.35f,.35f,-.2f,.2f});
        near(medium.distance,1.75f,1e-3f);
        // An off-centre lens keeps its centre: only its width counts.
        const auto off=native_eyes::stereoEyes(stereo,{-.3f,.4f,-.2f,.2f});
        near(off.shift,.35f*.02f,1e-6f);
    });
    test("the game screen is in stereo only while the game plays a scene", [] {
        SceneClock clock;
        check(!clock.running(false,1000),"a game that never ran had a scene");
        check(clock.running(true,1016),"a frame that took game time did not count");
        check(clock.running(false,1016+sceneHoldMs),"a slow frame ended the scene");
        // The pause menu: the game's frames take no game time.
        check(!clock.running(false,1017+sceneHoldMs),"a paused game kept its scene");
        check(clock.running(true,5000),"the scene did not come back with the game's time");
        check(!clock.running(false,4000) && clock.running(true,4010),"a clock reversal");
        // The scene's eyes need the HUD's panel on their screen: it carries the subtitles.
        ScenePanel panel;
        check(!panel.wanted(false,false,900),"eyes asked for without a scene");
        check(panel.wanted(true,false,1000) && panel.wanted(true,false,1000+scenePanelMs),
              "a scene's eyes given up before its panel could come");
        check(!panel.wanted(true,false,1001+scenePanelMs),"eyes without a panel went on");
        check(!panel.wanted(true,true,3000),"a scene given up asked again");
        check(!panel.wanted(false,false,3100) && panel.wanted(true,false,3200),"the next scene did not ask again");
        for(uint64_t t=3300;t<9000;t+=100)check(panel.wanted(true,true,t),"a scene with its panel stopped");
        check(panel.wanted(true,false,8900+scenePanelMs) && !panel.wanted(true,false,8901+scenePanelMs),
              "a panel that left was waited for too briefly, or for ever");
        check(!panel.wanted(false,false,20000) && panel.wanted(true,false,100) && panel.wanted(true,false,50),
              "a clock reversal");
        near(eyeDistance({-.032f,1.6f,0},{.032f,1.6f,0}),.064f);
        near(eyeDistance({-.029f,1.6f,0},{.029f,1.6f,0}),.058f);
        near(eyeDistance({0,0,0},{0,0,0}),.064f);
        near(eyeDistance({0,0,0},{.2f,0,0}),.064f);
        near(eyeDistance({0,0,0},{std::numeric_limits<float>::quiet_NaN(),0,0}),.064f);
    });
    test("movement requires a recent new headset image", [] {
        check(!recentPresentation(0,1000),"controls started before a first image");
        check(recentPresentation(1000,1000),"current image rejected");
        // A stutter of a few hundred milliseconds must not drop the webs.
        check(recentPresentation(1000,1500),"valid image lease rejected");
        check(!recentPresentation(1000,1501),"stalled presentation kept controls active");
        check(!recentPresentation(1000,999),"clock reversal accepted");
        check(recentPresentation(1300,1301),"new image did not restore the gate");
        check(controlHoldMs < imageHoldMs, "controls outlast the image they are steered by");
    });
    test("without gameplay the headset shows the game's own frame on a screen, not black", [] {
        GameScreen screen;
        check(!screen.update(true,1000) && !screen.holding(),"gameplay put the screen up");
        // Pause menu, hint card or cutscene: the last immersive image first.
        check(!screen.update(false,1100) && screen.holding(),"the last image was not held");
        check(!screen.update(false,1100+screenDelayMs-1),"a short camera gap put the screen up");
        check(screen.update(false,1100+screenDelayMs) && screen.entered() && !screen.holding(),
              "the game's camera did not replace the held image");
        check(screen.update(false,5000) && !screen.entered(),"the screen was placed again while up");
        check(screenDelayMs < imageHoldMs,"the held image runs out before the screen comes up");
        // Gameplay returns: immersive at once, and a later gap holds again.
        check(!screen.update(true,5001) && !screen.holding(),"gameplay did not take the screen down");
        check(!screen.update(false,6000) && screen.holding(),"a new gap did not hold the image");
        check(!screen.update(true,6100),"a gap shorter than the delay flashed the screen");
        check(!screen.update(false,7000),"the delay did not restart");
        screen.reset();
        check(!screen.update(false,7000) && screen.holding(),"reset kept the old gap");
        check(!screen.update(false,6999),"clock reversal put the screen up");
    });
    test("the virtual screen stands level in front of the head", [] {
        auto ahead=[](float pitch,float yaw,float roll) {
            const auto axis=[](Vec3 v,float a) {return Quat{v.x*std::sin(a/2),v.y*std::sin(a/2),v.z*std::sin(a/2),std::cos(a/2)};};
            return screenAhead({{1,1.7f,2},axis({0,1,0},yaw)*axis({1,0,0},pitch)*axis({0,0,1},roll)});
        };
        // Straight up or down, the top of the head gives the heading; a rolled
        // head there faces elsewhere, so those are checked level.
        for (float pitch : {0.f,-.6f,.5f,-1.5707f,1.5707f})
            for (float roll : {0.f,.4f}) {
                if (std::abs(pitch)>1.5f && roll!=0)
                    continue;
                const auto pose=ahead(pitch,.8f,roll);
                // Facing back at the viewer from 2.5 m along the head's heading.
                const Vec3 heading{-std::sin(.8f),0,-std::cos(.8f)};
                near(pose.position.x,1+heading.x*2.5f);
                near(pose.position.y,1.7f);
                near(pose.position.z,2+heading.z*2.5f);
                const auto back=pose.orientation.rotate({0,0,1}), up=pose.orientation.rotate({0,1,0});
                near(dot(back,heading),-1);
                near(up.y,1);
            }
    });
    test("on the game screen the VR controllers are the game's Xbox controller", [] {
        using namespace game_pad;
        XrFrame f;
        f.buttons=buttonA|buttonY|buttonMenu;
        f.hands[0].stickX=-1;f.hands[0].stickY=.05f;f.hands[0].trigger=.5f;f.hands[0].squeeze=.9f;
        f.hands[1].stickY=.6f;f.hands[1].trigger=1;f.hands[1].stickClick=true;
        const auto menus=fromControllers(f,Mapping::menus);
        check(menus.buttons==(a|y|start|leftShoulder|rightThumb),"menu buttons");
        // Full deflection, a stick at rest a few percent off centre, analog triggers.
        check(menus.thumbLX==-32767 && menus.thumbLY==0 && menus.thumbRX==0 && menus.thumbRY==19660,"sticks");
        check(menus.leftTrigger==128 && menus.rightTrigger==255,"triggers");
        // In VR the controllers swing: of their own buttons only pause (menu) and the game menu (X) pass.
        check(fromControllers(f,Mapping::gameplay)==game_pad::State{static_cast<uint16_t>(start)},"Y alone reached the game in play");
        f.buttons=buttonA|buttonX|buttonMenu;
        const auto play=fromControllers(f,Mapping::gameplay);
        check(play==game_pad::State{static_cast<uint16_t>(start|back)},"gameplay passes only Start and Back");
        // Walking and jumping come as the swing leaves them: the October 6
        // session could not move at all while they went to the keyboard only.
        const auto walking=fromControllers(f,Mapping::gameplay,{.5f,-1,true});
        check(walking.buttons==(start|back|a),"native jump is not A");
        check(walking.thumbLX==16384 && walking.thumbLY==-32767 && !walking.thumbRX && !walking.thumbRY,"walk stick");
        check(!walking.leftTrigger && !walking.rightTrigger,"triggers leaked into gameplay");
        check(fromControllers(f,Mapping::gameplay,{.05f,0,false})==play,"stick noise walked");
        check(fromControllers(f,Mapping::none,{1,1,true})==game_pad::State{},"nothing without the headset");
        // Y is the game's Y in VR (interact, web strike) and B its B (the dodge), as the swing leaves them.
        check(fromControllers(f,Mapping::gameplay,{0,0,false,true}).buttons==(start|back|y),"interact is not the game's Y");
        check(fromControllers(f,Mapping::gameplay,{0,0,false,false,true}).buttons==(start|back|b),"dodge is not the game's B");
        // A dodge goes the way the stick points: both reach the game together.
        const auto dodging=fromControllers(f,Mapping::gameplay,{-1,0,false,false,true});
        check(dodging.buttons==(start|back|b) && dodging.thumbLX==-32767 && !dodging.thumbLY,"a dodge lost its way");
        // The game's web pull is its LB and RB together; the grips alone are no bumpers in VR.
        check(fromControllers(f,Mapping::gameplay,{0,0,false,false,false,true}).buttons==(start|back|leftShoulder|rightShoulder),
              "a pull is not the game's LB + RB");
        // The right stick pushed down is the game's D-pad down (heal), pushed up its LT and RT together (the zip
        // to a point): until October 12 neither reached the game in VR play.
        check(fromControllers(f,Mapping::gameplay,{0,0,false,false,false,false,true}).buttons==(start|back|dpadDown),
              "a heal is not the game's D-pad down");
        const auto zipping=fromControllers(f,Mapping::gameplay,{0,0,false,false,false,false,false,true});
        check(zipping.buttons==(start|back) && zipping.leftTrigger==255 && zipping.rightTrigger==255,
              "a zip is not the game's LT + RT");
        XrFrame gripping;gripping.hands[0].squeeze=gripping.hands[1].squeeze=1;
        check(fromControllers(gripping,Mapping::gameplay)==game_pad::State{},"the grips were bumpers in VR play");
        XrFrame pressed;pressed.buttons=buttonB|buttonY;
        check(fromControllers(pressed,Mapping::menus).buttons==(b|y),"B and Y on the game screen are not the game's");
        check(fromControllers(pressed,Mapping::gameplay)==game_pad::State{},"B or Y reached the game without the swing's leave");
        XrFrame broken;
        broken.hands[0].stickX=std::numeric_limits<float>::quiet_NaN();
        broken.hands[1].trigger=std::numeric_limits<float>::infinity();
        check(fromControllers(broken,Mapping::menus)==game_pad::State{},"bad readings stay at rest");
    });
    test("the Steam Frame's View button and D-pad are the game's own, in play and on the game screen", [] {
        using namespace game_pad;
        XrFrame f;
        f.buttons=buttonView|buttonDpadDown;
        // In VR play: the game menu and the heal, with nothing else of the controllers.
        check(fromControllers(f,Mapping::gameplay)==game_pad::State{static_cast<uint16_t>(back|dpadDown)},
              "View and D-pad down are not the game's Back and D-pad down in play");
        check(fromControllers(f,Mapping::menus).buttons==(back|dpadDown),"View and D-pad down on the game screen");
        check(fromControllers(f,Mapping::none)==game_pad::State{},"they reached the game without the headset");
        f.buttons=buttonDpadUp|buttonDpadLeft|buttonDpadRight;
        check(fromControllers(f,Mapping::gameplay).buttons==(dpadUp|dpadLeft|dpadRight),"the D-pad in play");
        check(fromControllers(f,Mapping::menus).buttons==(dpadUp|dpadLeft|dpadRight),"the D-pad on the game screen");
        // The D-pad's heal and the right stick's are one button of the game's.
        f.buttons=buttonDpadDown;
        check(fromControllers(f,Mapping::gameplay,{0,0,false,false,false,false,true}).buttons==dpadDown,"two heals");
        // Held from play into the menu it opened, View is still the game's Back: no second press.
        PressOrigins o;
        f.buttons=buttonView|buttonX|buttonY;
        o.update(f.buttons,true);
        check(fromControllers(o.filtered(f,Mapping::gameplay),Mapping::gameplay).buttons==back,"View and X in play");
        o.update(f.buttons,false);
        check(fromControllers(o.filtered(f,Mapping::menus),Mapping::menus).buttons==back,"View let go by the switch");
        // The Frame's X and Y are Spidy's X and Y: the game menu and the interact, as Touch's.
        XrFrame face;face.buttons=buttonX;
        check(fromControllers(face,Mapping::gameplay).buttons==back,"X is not the game menu");
        check(fromControllers(face,Mapping::menus).buttons==x,"X on the game screen is not the game's X");
    });
    // The prompt and button work of October 13: the game names the button of
    // every prompt it raises, and VR play could send nine of the pad's
    // sixteen controls. These cover the whole pad instead of the moments
    // players have reported, which is how the gaps kept being found.
    test("on the game's own crawl the jump goes out as its RT + A, the only jump that leaves the wall", [] {
        using namespace game_pad;
        // Measured on the first mission's shaft, October 14: A alone, with the stick any way or none, only
        // leaps the hero along the wall; B, the bumpers, either trigger and a held A do nothing; RT + A
        // leaves it in 0.09 s. RT is all the parkour field adds - not LT, which with RT is the game's zip.
        XrFrame f;f.hands[0].valid=f.hands[1].valid=true;
        Walk jump{};jump.jump=true;jump.parkour=true;
        const auto s=fromControllers(f,Mapping::gameplay,jump);
        check((s.buttons&a)!=0,"the jump went out without A");
        check(s.rightTrigger==255&&s.leftTrigger==0,"the parkour jump was not RT alone");
        Walk plain{};plain.jump=true;
        const auto t=fromControllers(f,Mapping::gameplay,plain);
        check(t.rightTrigger==0&&t.leftTrigger==0,"a plain jump carried a trigger");
        // Nowhere but play: the menus get nothing of it.
        check(fromControllers(f,Mapping::menus,jump).rightTrigger==0,"the menus got the parkour trigger");
    });
    test("every control of the pad can be sent in VR play, by gameplay or the pad layer", [] {
        using namespace game_pad;
        using prompt_buttons::Demand;
        // What a mapping can be made to send, over every control of the VR
        // controllers: each button, each grip, each trigger, each stick click
        // and the right stick's four ways, alone and in the pairs that matter.
        const auto sendable=[](Mapping mapping){
            std::vector<State> out;
            // Touch and Index, which is what reachableInPlay answers for: the
            // Steam Frame's own View and D-pad are left out here (only that
            // controller has them, and its own test covers them).
            const uint32_t faces[]={0,buttonA,buttonB,buttonX,buttonY,buttonMenu};
            // The swing's leave, as game_xr fills it: every flag on its own.
            Walk walks[9]{};
            walks[1].jump=true;walks[2].interact=true;walks[3].dodge=true;walks[4].pull=true;
            walks[5].heal=true;walks[6].zip=true;walks[7].scan=true;walks[8].right=1;
            for(uint32_t face:faces)
                for(const auto& walk:walks)
                    for(int grips=0;grips<4;++grips)
                        for(int triggers=0;triggers<4;++triggers)
                            for(int clicks=0;clicks<4;++clicks)
                                for(int way=0;way<5;++way){
                                    XrFrame f;f.buttons=face;
                                    for(unsigned h=0;h<2;++h){
                                        f.hands[h].valid=true;
                                        f.hands[h].squeeze=grips&(1<<h)?1.f:0.f;
                                        f.hands[h].trigger=triggers&(1<<h)?1.f:0.f;
                                        f.hands[h].stickClick=(clicks&(1<<h))!=0;
                                    }
                                    const float ways[5][2]={{0,0},{0,1},{0,-1},{-1,0},{1,0}};
                                    f.hands[1].stickX=ways[way][0];f.hands[1].stickY=ways[way][1];
                                    out.push_back(fromControllers(f,mapping,walk));
                                }
            return out;
        };
        const auto play=sendable(Mapping::gameplay),layer=sendable(Mapping::pad);
        // A control is sendable when some combination holds its bits and
        // nothing else of the pad: a press the player can make on purpose.
        const auto holds=[](const std::vector<State>& states,Demand d){
            for(const auto& s:states){
                if(s.buttons!=d.buttons)continue;
                if((s.leftTrigger>200)!=d.leftTrigger||(s.rightTrigger>200)!=d.rightTrigger)continue;
                return true;
            }
            return false;
        };
        std::string missing;
        for(const auto& control:prompt_buttons::controls()){
            const bool inPlay=holds(play,control.demand),inLayer=holds(layer,control.demand);
            if(!inPlay&&!inLayer)missing+=std::string(missing.empty()?"":", ")+control.name;
            // What prompt_buttons tells a player matches what the mapping does.
            check(prompt_buttons::reachableInPlay(control.demand)==inPlay,
                  std::string("reachableInPlay disagrees with the gameplay mapping for ")
                      .append(control.name).c_str());
        }
        check(missing.empty(),("VR play cannot send "+missing).c_str());
        // And the nine the pad layer is there for: play alone never had them.
        using prompt_buttons::padX;using prompt_buttons::padLeftThumb;using prompt_buttons::padDpadUp;
        using prompt_buttons::padDpadLeft;using prompt_buttons::padDpadRight;
        using prompt_buttons::padLeftShoulder;using prompt_buttons::padRightShoulder;
        const Demand alone[]={{padX},{padLeftThumb},{padDpadUp},{padDpadLeft},{padDpadRight},
                              {padLeftShoulder},{padRightShoulder},
                              {0,true,false},{0,false,true}};
        for(const auto& d:alone){
            check(!holds(play,d),"a control the pad layer is for reached the game in play after all");
            check(holds(layer,d),"the pad layer does not send one of the nine");
            check(!prompt_buttons::reachableInPlay(d),"reachableInPlay called one of the nine reachable");
        }
    });
    test("the game played on its screen: a tap of the menu button pauses, held it is Back, and the left stick's "
         "click with the right stick is the D-pad", [] {
        using namespace game_pad;
        const float dt=1.f/90;
        // `seconds` of frames, each through the screen controls: the pad of the last.
        const auto run=[&](ScreenControls& c,float seconds,const XrFrame& f,bool play=true){
            State s;
            for(float t=0;t<seconds-1e-4f;t+=dt){s=fromControllers(f,Mapping::menus);c.apply(dt,play,f,s);}
            return s;
        };
        XrFrame rest,menu;menu.buttons=buttonMenu;
        // A tap: nothing while it is down, Start as it is let go, for a tenth of a second.
        {
            ScreenControls c;
            check(!(run(c,.2f,menu).buttons&(start|back)),"the menu button went out before it was let go");
            check(run(c,dt,rest).buttons==start,"a tap of the menu button is no Start");
            check(run(c,.2f,rest).buttons==0,"Start stayed down");
        }
        // Held: Back from half a second on, and nothing as it is let go.
        {
            ScreenControls c;
            check(run(c,.4f,menu).buttons==0,"Back came too early");
            check(run(c,.2f,menu).buttons==back,"the menu button held is not Back");
            check(run(c,dt,rest).buttons==0 && run(c,.2f,rest).buttons==0,"a held menu button paused as well");
        }
        // A press from the pause menu, still down as the game comes back, does nothing until it is let go.
        {
            ScreenControls c;
            run(c,.1f,menu,false);
            check(run(c,.6f,menu).buttons==0 && run(c,dt,rest).buttons==0,"the pause menu's Start came back");
        }
        // The left stick's click with the right stick pushed: the D-pad, turning no camera; no L3 after it.
        {
            ScreenControls c;
            XrFrame f;f.hands[0].stickClick=true;f.hands[1].valid=true;f.hands[1].stickY=-.9f;
            const auto s=run(c,.2f,f);
            check(s.buttons==dpadDown && s.thumbRX==0 && s.thumbRY==0,"the left click and a push down is no D-pad down");
            f.hands[1].stickY=0;f.hands[1].stickX=.8f;
            check(run(c,dt,f).buttons==dpadRight,"a push right is no D-pad right");
            check(run(c,dt,rest).buttons==0,"an L3 after the D-pad");
        }
        // The left click alone is the game's L3 as it is let go; with the right stick's click, the two together
        // as both are let go, and nothing of either while they are down.
        {
            ScreenControls c;
            XrFrame f;f.hands[0].stickClick=true;
            check(run(c,.2f,f).buttons==0,"L3 before it was let go");
            check(run(c,dt,rest).buttons==leftThumb,"a click of the left stick is no L3");
            run(c,.2f,rest);
            f.hands[1].stickClick=true;
            check(!(run(c,.2f,f).buttons&(leftThumb|rightThumb)),"a thumb button went out with both sticks down");
            XrFrame right;right.hands[1].stickClick=true;
            check(!(run(c,dt,right).buttons&(leftThumb|rightThumb)),"the right click went on alone after the left");
            check(run(c,dt,rest).buttons==(leftThumb|rightThumb),"both clicks are not the game's L3 + R3");
            check(run(c,.2f,rest).buttons==0,"L3 + R3 stayed down");
        }
        // Both held on into the pause (PausePress has its Start): no L3 + R3 as they are let go.
        {
            ScreenControls c;
            XrFrame f;f.hands[0].stickClick=f.hands[1].stickClick=true;
            check(!(run(c,pauseHoldSeconds+.1f,f).buttons&(leftThumb|rightThumb)),"a thumb button under the pause");
            check(run(c,dt,rest).buttons==0,"the pause's chord was L3 + R3 too");
        }
        // Not played on the screen (its menus, VR): as the game screen has it.
        {
            ScreenControls c;
            check(run(c,.6f,menu,false).buttons==start,"the menu button is not Start in the game's menus");
        }
    });
    test("the gadget wheel: held, it opens in front of the hand; the hand's way lights a gadget; let go, it is "
         "chosen", [] {
        const float dt=1.f/90;
        const Vec3 head{0,1.7f,0},hand{.2f,1.2f,-.4f};
        // `seconds` of updates with the hand at `at`: the events of all of them.
        const auto run=[&](GadgetWheel& w,float seconds,bool held,Vec3 at,uint8_t places,bool allowed=true){
            GadgetWheel::Event all;
            for(float t=0;t<seconds-1e-4f;t+=dt){
                const auto e=w.update(dt,held,allowed,at,head,places);
                all.tap|=e.tap;all.opened|=e.opened;all.lit|=e.lit;
                if(e.chosen>=0)all.chosen=e.chosen;
            }
            return all;
        };
        const uint8_t all=0xff;
        // A short press is the button's own; nothing opens.
        {
            GadgetWheel w;
            check(!run(w,.2f,true,hand,all).opened && !w.open(),"a short press opened the wheel");
            check(run(w,dt,false,hand,all).tap,"a short press is no tap");
        }
        // Held, it opens where the hand is, facing the eyes, upright.
        GadgetWheel w;
        check(run(w,.35f,true,hand,all).opened && w.open(),"held, the wheel did not open");
        const auto& f=w.frame();
        near(length(f.centre-hand),0,1e-6f);
        // Its up as near the world's as facing the eyes allows.
        const Vec3 upright=normalized(Vec3{0,1,0}-f.out*f.out.y);
        check(dot(f.out,normalized(head-hand))>.999f && dot(f.up,upright)>.999f && dot(cross(f.up,f.out),f.right)>.999f,
              "the wheel does not face the eyes, upright");
        check(w.lit()<0,"a gadget lit with the hand still");
        // To the player's right: the place there (2 of 8, a quarter turn clockwise from the top).
        check(run(w,.05f,true,hand+f.right*.08f,all).lit && w.lit()==2,"the hand to the right lit no gadget there");
        // How long it has been lit, for its snap and its boil.
        run(w,.1f,true,hand+f.right*.08f,all);
        check(w.litAge()>.12f && w.litAge()<.16f,"the lit gadget's time is wrong");
        // Up and a little left: the top one; then back near the middle: none.
        run(w,.05f,true,hand+f.up*.09f-f.right*.01f,all);
        check(w.lit()==0,"the hand up lit no top gadget");
        check(w.litAge()<.06f,"another gadget lit kept the last one's time");
        run(w,.05f,true,hand+f.up*.03f,all);
        check(w.lit()<0,"a gadget stayed lit with the hand back in the middle");
        // Let go there: nothing chosen.
        check(run(w,dt,false,hand,all).chosen<0 && !w.open(),"let go in the middle, something was chosen");
        // Only two gadgets: the nearer of them to the hand's way.
        GadgetWheel two;
        run(two,.35f,true,hand,0b10001);
        const auto& g=two.frame();
        run(two,.05f,true,hand+(g.up+g.right)*.06f,0b10001);
        check(two.lit()==0,"the hand up and right did not light the top gadget");
        check(run(two,dt,false,hand+(g.up+g.right)*.06f,0b10001).chosen==0,"let go on a lit gadget, it was not chosen");
        // Play ending with it open closes it choosing nothing; a press held through it does nothing.
        GadgetWheel cut;
        run(cut,.35f,true,hand,all);
        run(cut,.05f,true,hand+f.right*.08f,all);
        check(run(cut,dt,true,hand,all,false).chosen<0 && !cut.open(),"the wheel stayed open out of play");
        const auto after=run(cut,.5f,true,hand,all);
        check(!after.opened && !cut.open() && !run(cut,dt,false,hand,all).tap,"a press held through the end of play did something");
    });
    test("the gadget wheel draws its gadgets, names that the font has, and nothing while shut", [] {
        for(int g=1;g<=static_cast<int>(Gadget::gameWebShooter);++g){
            const char* name=gadgetName(static_cast<Gadget>(g));
            check(*name,"a gadget without a name");
            for(const char* c=name;*c;++c)check(*c==' '||drawable(*c),"a gadget's name has a letter the font lacks");
        }
        const Vec3 viewer{0,1.6f,0};
        const float pixel=.0007f; // a headset's, about
        GadgetWheelView view;
        std::vector<Vertex> v;
        appendGadgetWheel(v,view,viewer,pixel);
        check(v.empty(),"a shut wheel drew something");
        view.open=true;view.age=1;view.frame.centre={0,1.2f,-.4f};
        view.places[0]=Gadget::webShooter;view.places[2]=Gadget::impactWeb;view.most[2]=5;view.ammo[2]=3;
        view.lit=2;view.equipped=0;
        appendGadgetWheel(v,view,viewer,pixel);
        check(v.size()>300 && v.size()%3==0,"the wheel drew too little");
        for(const auto& vertex:v)
            check(length(vertex.position-view.frame.centre)<.3f,"the wheel drew outside itself");
        // Nothing for a viewer or a pixel that is no number.
        for(const float bad:{0.f,-1.f,std::numeric_limits<float>::quiet_NaN()}){
            std::vector<Vertex> none;
            appendGadgetWheel(none,view,viewer,bad);
            appendGadgetWheel(none,view,{std::numeric_limits<float>::quiet_NaN(),0,0},pixel);
            check(none.empty(),"a bad viewer or pixel drew a wheel");
        }
    });
    test("the gadget wheel is printed: a picture for each gadget, the lit one red with its name, landing in "
         "poses", [] {
        const Vec3 viewer{0,1.6f,0};
        const float pixel=.0007f;
        // The style's plates (web_visual.cpp's).
        const Vec3 red{.7682f,.0194f,.0284f},yellow{1.f,.7529f,.0742f},paper{.9301f,.9131f,.8550f},
            ink{.0033f,.0030f,.0070f};
        const auto is=[](Vec3 a,Vec3 b){return length(a-b)<2e-3f;};
        GadgetWheelView base;
        base.open=true;base.age=1;base.frame.centre={0,1.2f,-.4f};
        base.places[0]=Gadget::webShooter;base.equipped=0;
        const auto draw=[&](const GadgetWheelView& view){
            std::vector<Vertex> v;
            appendGadgetWheel(v,view,viewer,pixel);
            return v;
        };
        // Each gadget's badge has its picture: more drawn than for a badge with none (the game's own web
        // shooter, which the wheel never shows, has no picture).
        auto bare=base;
        bare.places[3]=Gadget::gameWebShooter;
        const size_t badgeAlone=draw(bare).size();
        for(int g=1;g<static_cast<int>(Gadget::gameWebShooter);++g){
            auto with=base;
            with.places[3]=static_cast<Gadget>(g);
            check(draw(with).size()>badgeAlone+60,"a gadget has no picture on its badge");
        }
        // The lit one is red, larger, and its name is lettered in a yellow caption across the middle; with
        // nothing lit the middle holds no caption.
        auto full=base;
        const Gadget all[]={Gadget::webShooter,Gadget::impactWeb,Gadget::webBomb,Gadget::electricWeb,
                            Gadget::tripMine,Gadget::concussionBlast,Gadget::spiderDrone,
                            Gadget::suspensionMatrix};
        for(int p=0;p<gadgetPlaces;++p){
            full.places[static_cast<size_t>(p)]=all[p];
            full.most[static_cast<size_t>(p)]=p?4:0;
            full.ammo[static_cast<size_t>(p)]=static_cast<uint8_t>(p==5?0:3);
        }
        const Vec3 place1=base.frame.centre+(base.frame.right*std::sin(gadgetPlaceAngle(1))+
                                             base.frame.up*std::cos(gadgetPlaceAngle(1)))*.13f;
        const auto reach=[&](const std::vector<Vertex>& v,Vec3 colour,Vec3 from){
            float most=0;
            for(const auto& x:v)
                if(is(x.color,colour)&&x.alpha>.99f)most=std::max(most,length(x.position-from));
            return most;
        };
        // Vertices of `colour` across the middle, inside the badges' ring (their nearest edge is 10 cm out).
        const auto middle=[&](const std::vector<Vertex>& v,Vec3 colour){
            size_t n=0;
            for(const auto& x:v)n+=is(x.color,colour)&&length(x.position-base.frame.centre)<.09f;
            return n;
        };
        auto shut=full;
        shut.lit=-1;
        const auto unlit=draw(shut);
        check(middle(unlit,yellow)==0 && reach(unlit,red,place1)==0,
              "with nothing lit, a caption or a red badge");
        auto lit=full;
        lit.lit=1;lit.litAge=1;
        const auto chosen=draw(lit);
        check(middle(chosen,yellow)>20,"the lit gadget's name has no yellow caption");
        check(middle(chosen,ink)>300+middle(unlit,ink),"the caption has no lettering");
        const float litRadius=reach(chosen,red,place1);
        check(litRadius>.036f && litRadius<.042f,"the lit badge is not red and larger than the rest");
        // The badge out of charges is grey, the equipped one yellow, the rest paper.
        const Vec3 grey=paper+(ink-paper)*.5f;
        const Vec3 place5=base.frame.centre+(base.frame.right*std::sin(gadgetPlaceAngle(5))+
                                             base.frame.up*std::cos(gadgetPlaceAngle(5)))*.13f;
        const Vec3 place0=base.frame.centre+base.frame.up*.13f;
        check(reach(chosen,grey,place5)>.025f,"a badge out of charges is not grey");
        check(reach(chosen,yellow,place0)>.025f,"the equipped badge is not yellow");
        check(reach(chosen,paper,base.frame.centre)>.1f,"the other badges are not paper");
        // It snaps on as it lights: a pose wide, a pose short, then at rest.
        auto snapping=lit;
        snapping.litAge=0;
        check(reach(draw(snapping),red,place1)>litRadius*1.1f,"the lit badge did not snap on wide");
        snapping.litAge=1.5f/12;
        check(reach(draw(snapping),red,place1)<litRadius,
              "the lit badge's second pose is not short of its size");
        // The wheel lands as it opens: its first pose small, then past its size, then at rest.
        auto opening=lit;
        opening.age=0;
        const auto extent=[&](const std::vector<Vertex>& v){
            float most=0;
            for(const auto& x:v)most=std::max(most,length(x.position-base.frame.centre));
            return most;
        };
        const float rest=extent(chosen);
        check(extent(draw(opening))<rest*.5f,"the wheel's first pose is not small");
        opening.age=1.5f/12;
        check(extent(draw(opening))>rest*1.08f,"the wheel's second pose is not past its size");
        // All of it, as large as it gets (every place full of charges, landing out of register), stays
        // within the overlay's room for a fight's printed marks.
        auto most=lit;
        most.age=0;most.litAge=0;
        for(int p=1;p<gadgetPlaces;++p)most.most[static_cast<size_t>(p)]=most.ammo[static_cast<size_t>(p)]=8;
        const auto heaviest=draw(most);
        check(heaviest.size()<60000 && heaviest.size()%3==0,"the wheel draws too much");
    });
    test("the pad layer: a click of both sticks latches it as it is let go, a web or idleness lets it go", [] {
        using game_pad::PadLayer;
        const auto run=[](PadLayer& l,float seconds,bool allowed,bool left,bool right,bool busy=false){
            bool on=false;
            for(float t=0;t<seconds;t+=1.f/90)on=l.update(1.f/90,allowed,left,right,busy);
            return on;
        };
        // A click: held chordSeconds, then let go.
        const auto click=[&](PadLayer& l){run(l,PadLayer::chordSeconds+.05f,true,true,true);return run(l,1.f/90,true,false,false);};
        PadLayer layer;
        check(!run(layer,.3f,true,false,false),"latched with no chord");
        check(!run(layer,.05f,true,true,true) && !run(layer,1.f/90,true,false,false),"latched before chordSeconds");
        check(!run(layer,PadLayer::chordSeconds+.05f,true,true,true),"latched under the thumbs");
        check(run(layer,1.f/90,true,false,false),"the click did not latch the layer as it was let go");
        check(layer.entries()==1,"the entry was not counted");
        check(run(layer,.2f,true,false,false),"letting the chord go let the layer go");
        check(!click(layer),"the second click did not let it go");
        check(layer.entries()==1,"letting go counted as an entry");
        // Held on into the pause (PausePress), the chord toggles nothing.
        PadLayer paused;
        check(!run(paused,game_pad::pauseHoldSeconds+.1f,true,true,true) && !run(paused,.2f,true,false,false),
              "the pause's hold latched the layer");
        check(paused.entries()==0,"the pause's hold counted as an entry");
        // One stick alone is no chord (it is the pull and the slow motion).
        PadLayer single;
        check(!run(single,1.f,true,false,true)&&!run(single,1.f,true,true,false),"one stick latched the layer");
        // A web carrying the player ends it, and the chord it was holding is spent.
        PadLayer carried;
        check(click(carried),"the click did not latch");
        run(carried,.1f,true,true,true);
        check(!carried.update(1.f/90,false,true,true,false),"a web did not end the layer");
        check(!run(carried,.3f,true,true,true) && !run(carried,1.f/90,true,false,false),
              "a chord held through a web latched it again");
        // Nothing pressed for idleSeconds and it lets go by itself; a press keeps it.
        PadLayer idle;
        click(idle);
        run(idle,.2f,true,false,false);
        check(run(idle,PadLayer::idleSeconds-1,true,false,false,true),"a busy layer let go");
        check(!run(idle,PadLayer::idleSeconds+1,true,false,false,false),"an idle layer did not let go");
    });
    test("both thumbsticks held in are the game's Start, once a hold, where the runtime keeps the menu button",
         [] {
        using game_pad::PausePress;
        const float dt=1.f/90;
        // `seconds` of updates: whether Start went out in any of them.
        const auto run=[&](PausePress& p,float seconds,bool allowed,bool left,bool right){
            bool start=false;
            for(float t=0;t<seconds-1e-4f;t+=dt)start|=p.update(dt,allowed,left,right);
            return start;
        };
        PausePress p;
        // Under the hold nothing; at pauseHoldSeconds Start, for a tenth of a second, once.
        check(!run(p,game_pad::pauseHoldSeconds-.05f,true,true,true),"Start before the hold");
        check(run(p,.2f,true,true,true) && p.pauses()==1 && p.spent(),"the hold was no Start");
        check(!run(p,1.f,true,true,true) && p.pauses()==1,"a hold held on paused again");
        // Spent until both sticks are let go: one let go is not enough.
        check(!run(p,.1f,true,true,false) && p.spent(),"one stick let go ended the hold");
        check(!run(p,dt,true,false,false) && !p.spent(),"both let go did not end it");
        check(run(p,game_pad::pauseHoldSeconds+.1f,true,true,true) && p.pauses()==2,"the next hold was no Start");
        run(p,dt,true,false,false);
        // A click of both (the pad layer's), one stick held (slow motion, the pull): no Start.
        check(!run(p,.3f,true,true,true) && !run(p,dt,true,false,false),"a click of both paused");
        check(!run(p,2.f,true,true,false) && !run(p,2.f,true,false,true),"one stick held paused");
        // With the controllers driving nothing, nothing; a chord held through that must be let go first.
        PausePress away;
        check(!run(away,.2f,false,true,true) && !run(away,1.f,true,true,true),"a chord held into play paused");
        run(away,dt,true,false,false);
        check(run(away,game_pad::pauseHoldSeconds+.1f,true,true,true),"after letting go, a hold was no Start");
    });
    test("the interact button sends what the game asks for, and keeps it for the press", [] {
        using game_pad::ContextPress;
        using prompt_buttons::Demand;
        const Demand pull{prompt_buttons::padLeftShoulder|prompt_buttons::padRightShoulder};
        const Demand vent{0,true,true};
        ContextPress press;
        check(!press.update(false,pull).any(),"a demand went out with no press");
        check(press.update(true,pull)==pull,"the press did not send the demand");
        check(press.presses()==1,"the press was not counted");
        // The demand it began with stands until the button is let go, even
        // after the prompt ends or another one takes its place.
        check(press.update(true,{})==pull,"the demand went when the prompt did");
        check(press.update(true,vent)==pull,"a new prompt changed the buttons under the thumb");
        check(!press.update(false,vent).any(),"the demand stayed after the press");
        check(press.update(true,vent)==vent,"the next press did not take the new demand");
        check(press.presses()==2,"the second press was not counted");
        // Y held while the hero walks up to a door takes the prompt that then shows.
        ContextPress walking;
        check(!walking.update(true,{}).any(),"a press with no prompt sent something");
        check(walking.presses()==0,"a press with no prompt was counted");
        check(walking.update(true,pull)==pull,"a prompt that arrived under the thumb was not taken");
        check(walking.presses()==1,"the arrival was not counted");
    });
    test("the game's icon ids say what a prompt asks of the pad, in every device family", [] {
        using namespace prompt_buttons;
        const Demand shoulders{padLeftShoulder|padRightShoulder},both{0,true,true};
        check(forIcon("XBButtonBothShoulders")==shoulders,"XBButtonBothShoulders");
        check(forIcon("XBButtonBothTriggers")==both,"XBButtonBothTriggers");
        check(forIcon("XBButtonA")==Demand{padA},"XBButtonA");
        check(forIcon("XBButtonASmall")==Demand{padA},"XBButtonASmall");
        check(forIcon("XBButtonX")==Demand{padX},"XBButtonX");
        check(forIcon("XBButtonDpadUp")==Demand{padDpadUp},"XBButtonDpadUp");
        check(forIcon("XBButtonDpadLeftRight")==Demand{padDpadLeft|padDpadRight},"XBButtonDpadLeftRight");
        check(forIcon("XBButtonStickRightPress")==Demand{padRightThumb},"XBButtonStickRightPress");
        check(forIcon("XBButtonLeftShoulderOutline")==Demand{padLeftShoulder},"an outline is its button");
        check(forIcon("XBButtonRightTrigger")==Demand{0,false,true},"XBButtonRightTrigger");
        check(forIcon("XB360ButtonStart")==Demand{padStart},"XB360ButtonStart");
        check(forIcon("XBOneButtonView")==Demand{padBack},"XBOneButtonView");
        // The same endings in the other eight families.
        check(forIcon("SteamButtonBothShoulders")==shoulders,"SteamButtonBothShoulders");
        check(forIcon("SwitchProButtonRightShoulder")==Demand{padRightShoulder},"SwitchProButtonRightShoulder");
        check(forIcon("SwitchProButtonPlus")==Demand{padStart},"SwitchProButtonPlus");
        check(forIcon("GenericButtonDpadDown")==Demand{padDpadDown},"GenericButtonDpadDown");
        check(forIcon("SteamButtonRightTriggerSoftpull")==Demand{0,false,true},"a soft pull is its trigger");
        // PlayStation names the face buttons by shape: its X is the pad's A.
        check(forIcon("PSButtonX")==Demand{padA},"PSButtonX is the pad's A");
        check(forIcon("PSButtonSquare")==Demand{padX},"PSButtonSquare is the pad's X");
        check(forIcon("PSButtonCircle")==Demand{padB},"PSButtonCircle is the pad's B");
        check(forIcon("PSButtonTriangle")==Demand{padY},"PSButtonTriangle is the pad's Y");
        check(forIcon("PSButtonDpadLeft")==Demand{padDpadLeft},"a PlayStation D-pad is still the D-pad");
        check(forIcon("PSDualShockButtonOptions")==Demand{padStart},"PSDualShockButtonOptions");
        // A stick pushed a way, and the sticks and keys that ask nothing of the pad.
        check(forIcon("LeftStickUp")==Demand{0,false,false,false,0,1},"LeftStickUp");
        check(forIcon("RightStickLeft")==Demand{0,false,false,false,0,0,-1,0},"RightStickLeft");
        check(!forIcon("XBButtonStickLeft").any(),"a stick with no way asks for a button");
        check(!forIcon("MkbKeyboardEnter").any(),"a keyboard key asks for a pad button");
        check(!forIcon("MkbMouseLeft").any(),"a mouse button asks for a pad button");
        check(!forIcon("GadgetTripmine").any(),"a gadget icon asks for a button");
        check(!forIcon("EmojiPoo").any(),"an emoji asks for a button");
        check(!forIcon("SteamButtonBackGripTopLeft").any(),"a back grip was read as the game's Back");
        // A word that only starts like an id is none.
        check(!forIcon("XBButtonAway").any(),"XBButtonAway was read as XBButtonA");
        check(!forIcon("").any(),"an empty id asks for something");
    });
    test("a prompt text says its buttons, and is rewritten with the VR controls", [] {
        using namespace prompt_buttons;
        const Demand shoulders{padLeftShoulder|padRightShoulder};
        // The hint line of the Shocker fight, as the October 11 research saw it.
        const auto thrown=forText("LB + RB (Hold) Throw");
        check(thrown.buttons==shoulders.buttons&&thrown.hold,"the hint line's buttons or its hold");
        check(forText("L1 + R1 Pull").buttons==shoulders.buttons,"the PlayStation spelling");
        check(forText("Press LT + RT to enter").leftTrigger&&forText("Press LT + RT to enter").rightTrigger,
              "the vent's triggers");
        // Spelled apart, the two still make the pair.
        check(forText("LB to aim, RB to fire").buttons==shoulders.buttons,"two spellings do not join");
        // The markup an icon comes in.
        check(forText("Tap <img id='XBButtonX'> repeatedly")==Demand{padX},"an icon id in markup");
        check(!forText("Open the door").any(),"a text with no button named one");
        check(!forText("Climb the ladder").any(),"a text with no button named one");
        char out[256];
        // Rewritten for a VR player: the control they have, not the pad's name.
        check(rewrite("LB + RB (Hold) Throw",out,sizeof out,false)&&!std::strcmp(out,"RIGHT STICK CLICK (Hold) Throw"),
              "the hint line was not rewritten for VR");
        check(rewrite("Press LT + RT to enter",out,sizeof out,false)&&
                  !std::strcmp(out,"Press RIGHT STICK UP to enter"),"the vent line was not rewritten");
        // An icon id is read but never rewritten: it sits inside the game's own
        // markup, and a name changed in there is markup it cannot resolve.
        check(!rewrite("Tap <img id='XBButtonX'> repeatedly",out,sizeof out,false),"an icon id was rewritten");
        check(!rewrite("Tap <img id='XBButtonX'> repeatedly",out,sizeof out,true),"an icon id was rewritten");
        // With the interact button serving prompts, what VR play has no control
        // for becomes Y; what it does have keeps the control's own name.
        check(rewrite("Tap LB to aim",out,sizeof out,true)&&!std::strcmp(out,"Tap Y to aim"),
              "the interact button was not offered for a bumper VR play cannot send");
        check(rewrite("LB + RB (Hold) Throw",out,sizeof out,true)&&
                  !std::strcmp(out,"RIGHT STICK CLICK (Hold) Throw"),
              "a control VR play has was replaced by the interact button");
        // A text that names no button is left to the game.
        check(!rewrite("Open the door",out,sizeof out,false),"a text with no button was rewritten");
        // A keyboard key is a token of the vocabulary with no pad button: untouched.
        check(!rewrite("Press MkbKeyboardEnter",out,sizeof out,false),"a keyboard prompt was rewritten");
        check(!rewrite("LB + RB",out,1,false),"a rewrite wrote into no room");
        // What the player is told matches what the mapping sends.
        check(!std::strcmp(vrControl(shoulders),"RIGHT STICK CLICK"),"the pull's VR control");
        check(!std::strcmp(vrControl(Demand{padX}),"PAD + X"),"the game's X needs the pad layer");
        check(!std::strcmp(vrControl(Demand{padY}),"Y"),"the game's Y is the interact");
        check(vrControl({})==nullptr,"an empty demand has a control");
        check(vrControl(Demand{padA|padB})==nullptr,"a mix of buttons has one control");
        // A held demand is the same control as a tapped one.
        check(vrControl(Demand{padY,false,false,true})==vrControl(Demand{padY}),"holding changed the control");
        // What the game ACTUALLY names its buttons with, found in it on October 13:
        // the markup token [BTN_B], in a text that reached Flash as an argument of
        // ShowText. Neither an icon id nor spelled letters, so nothing above fired.
        const char* line="When wall running horizontally, <span class='emphasis'>hold</span> "
                         "[BTN_B] to whip around a corner.";
        const auto whip=forText(line);
        check(whip.buttons==padB&&whip.hold,"the game's own prompt line named no button");
        check(forText("Press [BTN_L1] to aim")==Demand{padLeftShoulder},"[BTN_L1]");
        check(forText("Press [BTN_LB] to aim")==Demand{padLeftShoulder},"the Xbox spelling of the same");
        check(forText("[BTN_R1]")==Demand{padRightShoulder},"[BTN_R1]");
        check(forText("[BTN_L2]")==Demand{0,true,false},"[BTN_L2] is the left trigger");
        check(forText("[BTN_R2]")==Demand{0,false,true},"[BTN_R2] is the right trigger");
        check(forText("[BTN_A]")==Demand{padA}&&forText("[BTN_X]")==Demand{padX},"[BTN_A] or [BTN_X]");
        check(forText("[BTN_Y]")==Demand{padY}&&forText("[BTN_L3]")==Demand{padLeftThumb},"[BTN_Y] or [BTN_L3]");
        check(forText("[BTN_R3]")==Demand{padRightThumb},"[BTN_R3]");
        // Two tokens in one line make the pair the game wants held together.
        check(forText("Hold [BTN_L1] + [BTN_R1] to throw").buttons==shoulders.buttons,"a pair of tokens");
        // These two name no pad control, and a guess would press the wrong thing.
        check(!forText("[BTN_CANCEL] to go back").any(),"[BTN_CANCEL] was given a button");
        check(!forText("[BTN_MAP_WAYPOINT_SET]").any(),"[BTN_MAP_WAYPOINT_SET] was given a button");
        // A token that is none of the game's own stays unread.
        check(!forText("[BTN_NOPE]").any(),"an unknown token was given a button");
        // Rewritten: a control VR play has keeps its name; one it lacks becomes Y.
        check(rewrite(line,out,sizeof out,true)&&!std::strcmp(out,
              "When wall running horizontally, <span class='emphasis'>hold</span> B to whip around a corner."),
              "the game's own line was not rewritten for VR");
        check(rewrite("Press [BTN_L1] to aim",out,sizeof out,true)&&!std::strcmp(out,"Press Y to aim"),
              "a bumper VR play cannot send was not offered to the interact button");
        check(rewrite("Hold [BTN_L1] + [BTN_R1] to throw",out,sizeof out,false)&&
                  !std::strcmp(out,"Hold PAD + LEFT GRIP + PAD + RIGHT GRIP to throw"),
              "a pair of tokens was not rewritten");
        check(!rewrite("[BTN_CANCEL] to go back",out,sizeof out,true),"[BTN_CANCEL] was rewritten");
    });
    test("the QTE's own handles: the D-pad, a stick to move, and the keyboard's keys", [] {
        using namespace prompt_buttons;
        // The rest of the exe's prompt vocabulary, which is what a story beat
        // or an encounter asks with: the CineQTE handle table (image 0x387ae40
        // and up) and the stick motion action beside it. Reading only [BTN_*]
        // left every one of these a prompt Spidy could not answer.
        check(forText("[DPAD_UP]")==Demand{padDpadUp},"[DPAD_UP]");
        check(forText("[DPAD_DOWN]")==Demand{padDpadDown},"[DPAD_DOWN]");
        check(forText("[DPAD_LEFT]")==Demand{padDpadLeft},"[DPAD_LEFT]");
        check(forText("[DPAD_RIGHT]")==Demand{padDpadRight},"[DPAD_RIGHT]");
        // A QTE that asks for a stick to be moved names no way to move it.
        const auto stick=forText("Move [LS]");
        check(stick.any()&&stick.leftStickMotion&&!stick.rightStickMotion,"[LS] asks for the left stick");
        check(forText("[RS]").rightStickMotion,"[RS] asks for the right stick");
        check(reachableInPlay(stick),"the left stick is the player's own walking in VR play");
        check(!reachableInPlay(forText("[RS]")),"the right stick steers VR play: only the pad layer sends it");
        // The nine the keyboard and the mouse own: nothing for a pad to press,
        // and the game asking a device a player in a headset is not holding.
        for (const char* token : {"[MKB_CUSTOM00]","[MKB_CUSTOM01]","[MKB_CUSTOM02]","[MKB_CUSTOM03]",
                                  "[MKB_CUSTOM04]","[MKB_CUSTOM05]","[MKB_CUSTOM06]","[MKB_CUSTOM07]",
                                  "[MKB_CUSTOM08]"}) {
            check(!forText(token).any(),token);
            check(forText(token).keyboard,token);
        }
        check(forIcon("MkbKeyboardF").keyboard,"the keyboard's own icon was not seen as a key");
        check(forIcon("MkbMouseLeft").keyboard,"the mouse's icon was not seen as one");
        check(!forIcon("XBButtonA").keyboard,"a pad button was taken for a key");
        check(!forText("Open the door").keyboard,"a text that named nothing was taken for a key");
        // Every one of them survives the word a hook hands the VR thread.
        for (const char* token : {"[DPAD_UP]","[DPAD_DOWN]","[DPAD_LEFT]","[DPAD_RIGHT]","[LS]","[RS]",
                                  "[MKB_CUSTOM00]","[MKB_CUSTOM08]","[BTN_Y]","[BTN_L2]"}) {
            const auto asked=forText(token);
            check(unpack(pack(asked))==asked,token);
        }
        check(pack({})==0,"an empty demand packs to something");
        // What the player is told to do, and that a key leaves nothing to tell.
        check(!std::strcmp(vrControl(forText("[LS]")),"LEFT STICK"),"the left stick's VR control");
        check(!std::strcmp(vrControl(forText("[RS]")),"PAD + RIGHT STICK"),"the right stick's VR control");
        check(vrControl(forText("[MKB_CUSTOM03]"))==nullptr,"a key was given a VR control");
    });
    test("what the game's own crawl makes of the stick it is sent, measured", [] {
        using spidy::CrawlStick;
        // The wall x = 0: its surface is the y-z plane, up it is the world's
        // up, and the stick's own right there is ahead x up, as the rig lays
        // the axes out.
        const Vec3 n{1,0,0},ahead{0,1,0},side=cross(ahead,n);
        // The game took the stick and moved the body `theta` round from it.
        const auto feed=[&](CrawlStick& m,float w,float theta,float speed){
            m.update(n,ahead,std::sin(w),std::cos(w),(ahead*std::cos(w+theta)+side*std::sin(w+theta))*speed);
        };
        // A frame fixed against the surface: every stick comes out the same
        // quarter turn round, whichever way it was pushed. That is the answer
        // that wants one line of correction.
        CrawlStick fixed;
        for(int i=0;i<40;++i)feed(fixed,.157f*static_cast<float>(i),1.5707963f,3);
        check(fixed.samples()==40,"the samples were not taken");
        near(fixed.turn(),1.5707963f,.001f);near(fixed.agreement(),1,.001f);near(fixed.speed(),3,.01f);
        // The game taking it as sent: no turn at all, and the stick is right.
        CrawlStick straight;
        for(int i=0;i<40;++i)feed(straight,.157f*static_cast<float>(i),0,5);
        near(straight.turn(),0,.001f);near(straight.agreement(),1,.001f);near(straight.speed(),5,.01f);
        // A frame that is not fixed - the game's own camera swinging about as
        // the player looks around - agrees with nothing, and says the stick is
        // the wrong channel: the mover wants driving instead.
        CrawlStick loose;
        for(int i=0;i<40;++i)feed(loose,.3f*static_cast<float>(i),.157f*static_cast<float>(i),3);
        check(loose.samples()==40,"the samples were not taken");
        check(loose.agreement()<.2f,"a frame that was not fixed looked like one");
        // A stick barely off its rest is nobody's intent, and a body that is
        // not moving took no direction from it: neither is a sample.
        CrawlStick quiet;
        feed(quiet,0,0,3);
        check(quiet.samples()==1,"a pushed stick on a moving body was not a sample");
        quiet.update(n,ahead,0,CrawlStick::stick*.5f,ahead*3.f);
        quiet.update(n,ahead,0,1,ahead*(CrawlStick::moving*.5f));
        // Nor a body going straight into the wall, with nothing along it.
        quiet.update(n,ahead,0,1,n*-4.f);
        check(quiet.samples()==1,"a resting stick, a still body or a step into the wall was a sample");
        // A surface or an axis that is no vector, and numbers that are not
        // numbers, cannot carry a measurement.
        const float nan=std::numeric_limits<float>::quiet_NaN();
        CrawlStick odd;
        odd.update({},ahead,0,1,ahead*3.f);
        odd.update(n,{},0,1,ahead*3.f);
        odd.update(n,ahead,nan,1,ahead*3.f);
        odd.update(n,ahead,0,nan,ahead*3.f);
        odd.update(n,ahead,0,1,{nan,0,0});
        check(odd.samples()==0,"a measurement was taken from nothing");
        near(odd.turn(),0);near(odd.agreement(),0);near(odd.speed(),0);
    });
    test("indoors the game keeps the walls: the room says where the player is", [] {
        using spidy::Enclosure;
        const float open = 5.f;    // the ray found nothing within its reach
        const float close = 1.5f;  // a surface right there
        // A vent: closed above and from both pairs of sides. Three ways.
        const Enclosure::Room vent{close, close, close, close, close};
        check(Enclosure::closedWays(vent)==3,"a vent did not read as closed in");
        // A lift shaft: open above, closed on both pairs. Two ways - the shaft
        // of the first mission, climbed to the vent.
        const Enclosure::Room shaft{open, close, close, close, close};
        check(Enclosure::closedWays(shaft)==2,"a lift shaft did not read as closed in");
        // A corridor: a ceiling and two sides, open along its length.
        const Enclosure::Room corridor{close, open, open, close, close};
        check(Enclosure::closedWays(corridor)==2,"a corridor did not read as closed in");
        // An alley: close on one pair, open sky. One way, and not indoors - a
        // player wall running between two buildings keeps his walls.
        const Enclosure::Room alley{open, open, open, close, close};
        check(Enclosure::closedWays(alley)==1,"an alley read as closed in");
        // A street, a rooftop, a building face: nothing near.
        check(Enclosure::closedWays({open,open,open,open,open})==0,"the open city read as closed in");
        const Enclosure::Room face{open, close, open, open, open};
        check(Enclosure::closedWays(face)==0,"one wall read as closed in");
        // `seconds` is the frame's own length, as every rule here takes it.
        const Enclosure::Room city{open,open,open,open,open};
        const float frame=1.f/90;
        const auto hold=[&](Enclosure& e, Enclosure::Room room, float duration) {
            bool out=e.indoors();
            for (float t=0; t<duration; t+=frame)
                out=e.update(frame,room);
            return out;
        };
        Enclosure e;
        check(!e.update(frame,vent),"it called him indoors on the first frame");
        check(!hold(e,vent,Enclosure::settle*.5f),"it did not wait for the reading to hold");
        check(hold(e,vent,Enclosure::settle),"it never called him indoors");
        check(e.indoors()&&e.changes()==1,"the change was not counted");
        // Out again, and the same wait on the way out.
        check(hold(e,city,Enclosure::settle*.5f),"it left the moment one ray missed");
        check(!hold(e,city,Enclosure::settle),"it never came back out");
        check(e.changes()==2,"coming back out was not counted");
        // A single open frame in the middle of a vent does not take the walls
        // back: the reading has to hold.
        Enclosure f;
        hold(f,vent,Enclosure::settle*2);
        check(f.indoors(),"it never called him indoors");
        f.update(frame,city);
        check(f.indoors(),"one open frame took the walls back");
        check(hold(f,vent,Enclosure::settle),"the vent reading did not hold");
        check(f.changes()==1,"a flap was counted as a change");
        // A frame length that is no number, or absurd, cannot carry the verdict.
        Enclosure g;
        for (int i=0; i<200; ++i)
            g.update(std::numeric_limits<float>::quiet_NaN(),vent);
        check(!g.indoors(),"a frame length that was no number decided the verdict");
        Enclosure h;
        check(!h.update(1000.f,vent),"one absurd frame length decided the verdict");
    });
    test("nothing reaches the game until the headset has tracked once", [] {
        using game_pad::Mapping;
        using game_pad::mappingFor;
        // October 13: a session ran 414 frames with the headset never tracking,
        // the player's Touch B and A went out as the pad's, the title screen
        // walked to QUIT GAME and the game saved and closed.
        check(mappingFor(true,false,true,false)==Mapping::none,"a press went out before the headset tracked");
        check(mappingFor(true,false,false,false)==Mapping::none,"play went out before the headset tracked");
        check(mappingFor(true,false,false,true)==Mapping::none,"the pad layer went out before the headset tracked");
        // Without the headset or the focus, nothing goes out either, as before.
        check(mappingFor(false,true,true,false)==Mapping::none,"a press went out with no headset");
        check(mappingFor(false,false,false,false)==Mapping::none,"something went out with neither");
        // Once it has tracked, this is exactly what it always was.
        check(mappingFor(true,true,true,false)==Mapping::menus,"the game's own screen is not the full pad");
        check(mappingFor(true,true,true,true)==Mapping::menus,"the pad layer took the game's own screen");
        check(mappingFor(true,true,false,false)==Mapping::gameplay,"VR play is not the gameplay mapping");
        check(mappingFor(true,true,false,true)==Mapping::pad,"the latched layer is not the whole pad");
    });
    test("the interact hands the player back when the swing's wall is why the game offers nothing", [] {
        using game_pad::WallHandover;
        const float frame=1.f/90;
        const auto hold=[&](WallHandover& h, float duration, bool asking, bool wall, bool offered) {
            bool out=h.handing();
            for (float t=0; t<duration; t+=frame)
                out=h.update(frame,asking,wall,offered);
            return out;
        };
        // The vent: the swing holds him on a wall of its own, so the game has
        // withdrawn its interact and the press has nothing to land on.
        WallHandover h;
        check(!h.update(frame,false,true,false),"nothing pressed and it handed him back");
        check(h.update(frame,true,true,false),"a press on the swing's wall did not hand him back");
        check(h.handing()&&h.handovers()==1,"the hand-over was not counted");
        // It lasts while the player holds the button, then puts the walls back.
        check(hold(h,WallHandover::window*.5f,true,true,false),"it let go of the walls too early");
        // The game offering again while he is handed back is the hand-over
        // having worked; counted once, however many frames it offers for.
        check(h.update(frame,true,true,true),"the game's own offer ended the hand-over early");
        check(h.offered()==1,"the game's offer during a hand-over was not counted");
        check(h.update(frame,true,true,true)&&h.offered()==1,"one hand-over's offer was counted twice");
        check(!hold(h,WallHandover::window,true,true,false),"the walls did not come back");
        // Holding on does not start another: it takes a new press.
        check(!hold(h,.5f,true,true,false),"a held button started a second hand-over");
        check(h.handovers()==1,"a held button was counted twice");
        check(!h.update(frame,false,true,false),"letting go handed him back");
        check(h.update(frame,true,true,false),"the next press did not hand him back");
        check(h.handovers()==2,"the next press was not counted");
        // Letting go ends it, once the press has had its chance.
        check(hold(h,WallHandover::least*.5f,false,true,false),"it ended before the press could land");
        check(!hold(h,WallHandover::least,false,true,false),"letting go did not put the walls back");
        // Where the game IS offering something, the ordinary path works and
        // nothing is handed back.
        WallHandover offered;
        check(!offered.update(frame,true,true,true),"it handed him back while the game was offering an interact");
        check(offered.handovers()==0,"a hand-over was counted with an interact offered");
        check(offered.offered()==0,"an offer was counted without a hand-over");
        // And only for a wall of the swing's own: the game's own crawl already
        // lets him interact, and in the air there is no wall to leave.
        WallHandover elsewhere;
        check(!elsewhere.update(frame,true,false,false),"it handed him back off the swing's wall");
        check(elsewhere.handovers()==0,"a hand-over was counted off the swing's wall");
        // A frame length that is no number cannot keep the walls off for ever.
        WallHandover odd;
        check(odd.update(frame,true,true,false),"the press did not hand him back");
        for (int i=0; i<200; ++i)
            odd.update(std::numeric_limits<float>::quiet_NaN(),true,true,false);
        check(odd.handing(),"a frame length of no number ended it early");
        check(!hold(odd,WallHandover::window,true,true,false),"the walls never came back");
    });
    test("controller layouts: Touch and Index as they were, the Steam Frame's buttons each their own", [] {
        using Paths=std::vector<std::pair<ControllerInput,std::string>>;
        const auto of=[](ControllerKind kind) {
            for (const auto& layout : controllerLayouts())
                if (layout.kind==kind)
                    return layout;
            throw std::runtime_error("no such layout");
        };
        const auto paths=[](const ControllerLayout& layout) {
            Paths out;
            for (const auto& b : layout.bindings)
                out.push_back({b.input,b.path});
            return out;
        };
        // What every hand gives, whatever the controller; `squeeze` is the grip's own path.
        const auto hands=[](const char* squeeze) {
            Paths out;
            for (const std::string hand : {"/user/hand/left","/user/hand/right"}) {
                out.push_back({ControllerInput::aim,hand+"/input/aim/pose"});
                out.push_back({ControllerInput::grip,hand+"/input/grip/pose"});
                out.push_back({ControllerInput::trigger,hand+"/input/trigger/value"});
                out.push_back({ControllerInput::squeeze,hand+squeeze});
                out.push_back({ControllerInput::stick,hand+"/input/thumbstick"});
                out.push_back({ControllerInput::stickClick,hand+"/input/thumbstick/click"});
                out.push_back({ControllerInput::haptic,hand+"/output/haptic"});
            }
            return out;
        };
        using enum ControllerInput;
        // The bindings every release until 0.3.2 suggested, in their order.
        auto touch=hands("/input/squeeze/value");
        for (const auto& button : Paths{{a,"/user/hand/right/input/a/click"},{y,"/user/hand/left/input/y/click"},
                                        {b,"/user/hand/right/input/b/click"},{x,"/user/hand/left/input/x/click"},
                                        {menu,"/user/hand/left/input/menu/click"}})
            touch.push_back(button);
        check(paths(of(ControllerKind::touch))==touch,"the Touch layout changed");
        check(std::string(of(ControllerKind::touch).profile)=="/interaction_profiles/oculus/touch_controller" &&
              !of(ControllerKind::touch).extension,"the Touch profile changed");
        auto index=hands("/input/squeeze/force");
        for (const auto& button : Paths{{a,"/user/hand/right/input/a/click"},{y,"/user/hand/left/input/b/click"},
                                        {b,"/user/hand/right/input/b/click"},{x,"/user/hand/left/input/a/click"}})
            index.push_back(button);
        check(paths(of(ControllerKind::index))==index,"the Index layout changed");
        check(std::string(of(ControllerKind::index).profile)=="/interaction_profiles/valve/index_controller" &&
              !of(ControllerKind::index).extension,"the Index profile changed");
        // The Steam Frame: Valve's profile and extension, and only paths Valve's "Steam Frame Input" page lists.
        const auto frame=of(ControllerKind::frame);
        check(std::string(frame.profile)=="/interaction_profiles/valve/frame_controller_valve" &&
              std::string(frame.extension)=="XR_VALVE_frame_controller_interaction","the Steam Frame's profile");
        std::vector<std::string> valve;
        for (const std::string hand : {"/user/hand/left","/user/hand/right"})
            for (const char* path : {"/input/system/click","/input/bumper/click","/input/squeeze/click","/input/squeeze/value",
                                     "/input/trigger/click","/input/trigger/value","/input/thumbstick/click",
                                     "/input/thumbstick/x","/input/thumbstick/y","/input/thumbstick","/input/grip/pose",
                                     "/input/aim/pose","/output/haptic"})
                valve.push_back(hand+path);
        for (const char* path : {"/input/a/click","/input/b/click","/input/x/click","/input/y/click","/input/menu/click"})
            valve.push_back(std::string("/user/hand/right")+path);
        for (const char* path : {"/input/dpad_up/click","/input/dpad_left/click","/input/dpad_down/click",
                                 "/input/dpad_right/click","/input/view/click"})
            valve.push_back(std::string("/user/hand/left")+path);
        const auto framePaths=paths(frame);
        for (const auto& binding : framePaths)
            check(std::find(valve.begin(),valve.end(),binding.second)!=valve.end(),"a path the Steam Frame does not have");
        // Each hand gives what every controller's does.
        const auto both=hands("/input/squeeze/value");
        check(Paths(framePaths.begin(),framePaths.begin()+static_cast<ptrdiff_t>(both.size()))==both,"the Frame's hands");
        // The report: B, X and Y were one button. Every button is bound once, to a path of its own, on the
        // hand that has it.
        const std::pair<ControllerInput,const char*> buttons[]={
            {a,"/user/hand/right/input/a/click"},{b,"/user/hand/right/input/b/click"},
            {x,"/user/hand/right/input/x/click"},{y,"/user/hand/right/input/y/click"},
            {menu,"/user/hand/right/input/menu/click"},{view,"/user/hand/left/input/view/click"},
            {dpadUp,"/user/hand/left/input/dpad_up/click"},{dpadDown,"/user/hand/left/input/dpad_down/click"},
            {dpadLeft,"/user/hand/left/input/dpad_left/click"},{dpadRight,"/user/hand/left/input/dpad_right/click"}};
        check(framePaths.size()==both.size()+std::size(buttons),"the Frame's buttons");
        for (const auto& [input,path] : buttons)
            check(std::count(framePaths.begin(),framePaths.end(),std::pair<ControllerInput,std::string>{input,path})==1,
                  "a Steam Frame button is not its own");
        // No layout binds a control of the controllers twice, and only the Frame's has the Frame's own.
        for (const auto& layout : controllerLayouts()) {
            const auto all=paths(layout);
            for (const auto& [input,path] : all) {
                check(std::count_if(all.begin(),all.end(),[&](const auto& other) { return other.second==path; })==1,
                      "a control is bound twice");
                check(!frameOnly(input) || layout.kind==ControllerKind::frame,"another controller got the Frame's own");
            }
        }
        check(frameOnly(view) && frameOnly(dpadRight) && !frameOnly(menu) && !frameOnly(aim),"which controls are the Frame's own");
        check(std::string(controllerName(ControllerKind::frame))=="Steam Frame","the Frame's name");
    });
    test("B, X and Y stay what they were pressed as between the game screen and VR", [] {
        using namespace game_pad;
        const auto sent=[](const PressOrigins& o,uint32_t held,Mapping mapping,Walk walk={}) {
            XrFrame f;f.buttons=held;
            return fromControllers(o.filtered(f,mapping),mapping,walk).buttons;
        };
        PressOrigins o;
        // B pressed in VR dodges; the menu the game opens meanwhile gets no Back from it.
        o.update(buttonB,true);
        check(o.play()==buttonB && !o.screen(),"a press in VR is not play's");
        o.update(buttonB,false);
        check(o.play()==buttonB && !sent(o,buttonB,Mapping::menus),"a dodge held into a menu went Back there");
        // Let go and pressed on the screen it is Back, and no dodge once play is back.
        o.update(0,false);
        o.update(buttonB,false);
        check(sent(o,buttonB,Mapping::menus)==b,"B on the game screen is not Back");
        o.update(buttonB,true);
        check(!(o.play()&buttonB) && o.screen()==buttonB,"Back held into play dodged");
        o.update(0,true);
        check(!o.play() && !o.screen(),"a button let go stayed held");
        // X opens the game menu from VR; held on into the menu it is no X there.
        o.update(buttonX,true);
        check(sent(o,buttonX,Mapping::gameplay)==back,"X in VR is not the game menu");
        o.update(buttonX,false);
        check(!sent(o,buttonX,Mapping::menus),"the game menu's press was X in that menu");
        // X pressed in the menu is X there, and opens no menu when play is back.
        o.update(0,false);
        o.update(buttonX,false);
        check(sent(o,buttonX,Mapping::menus)==x,"X on the game screen is not the game's X");
        o.update(buttonX,true);
        check(!sent(o,buttonX,Mapping::gameplay),"X held from a menu opened the game menu");
        // Y interacts in VR; held into the game's Settings it resets no tab. A second button
        // pressed meanwhile is the screen's own; A and the menu button pass as they are.
        o.update(buttonY,true);
        o.update(buttonY|buttonB,false);
        check(sent(o,buttonY|buttonB|buttonA|buttonMenu,Mapping::menus)==(b|a|start),"buttons mixed up across the switch");
        check(!sent(o,buttonY|buttonB,Mapping::none),"buttons reached the game without the headset");
    });
    test("Y held walks the player up to what it is for; a tap, a fight and a wall do not", [] {
        using game_pad::InteractStep;
        const float dt=1.f/90;
        const float turn=InteractStep::turnSeconds;
        // Frames of Y held, allowed and moving at `speed`; the time the stick points ahead in them.
        const auto run=[&](InteractStep& s,float seconds,bool allowed=true,float speed=3) {
            float walked=0;
            for(float t=0;t<seconds;t+=dt)
                if(s.update(dt,true,allowed,speed)>0) walked+=dt;
            return walked;
        };
        InteractStep step;
        // A tap interacts where the player stands: the hero turns to it, and no walk begins.
        check(std::abs(run(step,.2f)-turn)<.03f && !step.steps(),"a tap of Y walked the player, or did not turn him");
        check(step.update(dt,false,true,0)==0,"Y let go still walked");
        // Held on, the walk begins after holdSeconds, at walkStick, and ends after walkSeconds.
        const float walked=run(step,5);
        check(step.steps()==1,"Y held began no walk, or more than one");
        check(std::abs(walked-turn-InteractStep::walkSeconds)<.04f,"the walk did not last walkSeconds");
        check(step.update(dt,true,true,3)==0,"a walk that ended went on while Y stayed held");
        step.update(dt,false,true,0);
        const uint32_t walks=step.steps();
        check(std::abs(run(step,InteractStep::holdSeconds-.05f)-turn)<.03f && step.steps()==walks,
              "the walk began before holdSeconds");
        check(step.update(.1f,true,true,3)==InteractStep::walkStick,"the walk is not at walkStick");
        check(step.steps()==2,"another press began no second walk");
        // At a wall the walk gets its start, then ends; the same hold walks no more.
        step.update(dt,false,true,0);
        const float blocked=run(step,3,true,0)-turn;
        check(blocked>InteractStep::startSeconds && blocked<InteractStep::startSeconds+InteractStep::stallSeconds+.05f,
              "a walk into a wall went on");
        check(run(step,1,true,3)==0,"a walk a wall ended began again in the same press");
        // Y pressed in a fight (the web strike), in the air or on a wall walks nobody, later in the hold neither.
        step.update(dt,false,true,0);
        const uint32_t before=step.steps();
        check(run(step,1,false)==0 && run(step,1,true)==0 && step.steps()==before,"a web strike walked the player");
        // A fight that begins on the way ends the walk.
        step.update(dt,false,true,0);
        check(run(step,1)>0 && step.update(dt,true,false,3)==0 && run(step,1)==0,"the walk went on into a fight");
        // Bad timing moves nobody, and holds no Y of its own.
        step.update(dt,false,true,0);
        check(step.update(std::numeric_limits<float>::quiet_NaN(),true,true,3)==0,"a bad frame time walked the player");
        check(step.pressed() && step.update(std::numeric_limits<float>::quiet_NaN(),false,true,0)==0 && !step.pressed(),
              "a press without a frame time went on");
    });
    test("a tap of Y turns the hero to what the player looks at and stays down while he turns", [] {
        using game_pad::InteractStep;
        const float dt=1.f/90;
        // A press of `held` seconds, then let go, `after` in all: how long the stick pointed ahead, and how long
        // the game's Y was down.
        struct Press { float stick{}, y{}; };
        const auto press=[&](InteractStep& s,float held,float after,bool allowed=true,float endsAt=1e9f) {
            Press p;
            for(float t=0;t<after;t+=dt) {
                if(s.update(dt,t<held,allowed && t<endsAt,0)>0) p.stick+=dt;
                if(s.pressed()) p.y+=dt;
            }
            return p;
        };
        InteractStep step;
        check(!step.pressed() && step.update(dt,false,true,0)==0 && !step.pressed(),"Y is down before any press");
        // A tap of a tenth of a second: the stick for turnSeconds, the game's Y for pressSeconds.
        const auto tap=press(step,.1f,2);
        check(std::abs(tap.stick-InteractStep::turnSeconds)<.03f,"a tap's turn is not turnSeconds long");
        check(std::abs(tap.y-InteractStep::pressSeconds)<.03f,"a tap's Y is not down for pressSeconds");
        check(!step.steps(),"a tap began a walk");
        // A press longer than that is down as long as the player holds it.
        const auto longer=press(step,1,2);
        check(std::abs(longer.y-1)<.03f,"a held Y is not the player's own");
        // In a fight Y is the web strike: the player's own press, and no turn.
        const auto strike=press(step,.1f,2,false);
        check(strike.stick==0 && std::abs(strike.y-.1f)<.03f,"a web strike turned the hero or was held on");
        // A fight that begins while a tap is held on lets the Y go.
        const auto cut=press(step,.1f,2,true,.3f);
        check(std::abs(cut.y-.3f)<.03f,"a tap was held on into a fight");
        // Two taps in a row are two presses: the game's Y comes up between them.
        InteractStep twice;
        press(twice,.1f,1);
        check(!twice.pressed(),"Y stayed down after the press's time");
        const auto second=press(twice,.1f,1);
        check(std::abs(second.stick-InteractStep::turnSeconds)<.03f,"a second tap did not turn the hero");
    });
    test("the game's own camera is turned to where the player looks", [] {
        using game_pad::CameraLook;
        const Vec3 north{0,0,-1}, east{1,0,0}, west{-1,0,0};
        const auto at=[](float degrees) {
            const float a=degrees*3.14159265f/180;
            return Vec3{std::sin(a),0,-std::cos(a)};
        };
        CameraLook look;
        // Looking the camera's way, or a little off it: the stick rests.
        check(look.update(north,north,true)==0 && look.error()==0 && !look.turns(),"an aligned camera was turned");
        check(look.update(at(CameraLook::startDegrees-2),north,true)==0 && !look.turns(),"a glance turned the camera");
        check(std::abs(look.error()-(CameraLook::startDegrees-2))<.1f,"the angle to the right is not its own");
        // To the camera's right the stick goes right, to its left left; far off, all the way.
        check(look.update(east,north,true)==1 && look.turns()==1,"a look to the right did not push the stick right");
        check(std::abs(look.error()-90)<.1f,"a quarter turn to the right is not 90 degrees");
        CameraLook left;
        check(left.update(west,north,true)==-1 && std::abs(left.error()+90)<.1f,"a look to the left did not push it left");
        // Once it turns it goes on to within stopDegrees, never weaker than the game's camera answers.
        const float near=look.update(at(CameraLook::stopDegrees+2),north,true);
        check(near>CameraLook::floor && near<.8f && look.turns()==1,"the turn stopped short, or crept");
        check(look.update(at(CameraLook::stopDegrees-1),north,true)==0,"the turn went on inside its rest");
        check(look.update(at(CameraLook::stopDegrees+2),north,true)==0,"a rested camera turned again for a little");
        // Where the head looks up or down counts for its level direction alone.
        CameraLook tilted;
        check(tilted.update(normalized(Vec3{1,2,0}),Vec3{0,-.5f,-1},true)==1,"looking up changed the way to turn");
        // Out of VR play, straight up, or without a reading: at rest, and the next turn starts anew.
        check(look.update(east,north,false)==0 && look.error()==0,"the camera was turned out of play");
        check(look.update(at(CameraLook::stopDegrees+2),north,true)==0,"a turn went on across a break");
        check(look.update({0,1,0},north,true)==0 && look.update(east,{0,1,0},true)==0,"a look straight up turned the camera");
        const float nan=std::numeric_limits<float>::quiet_NaN();
        check(look.update({nan,0,0},north,true)==0 && look.update(east,{0,0,nan},true)==0,"a bad reading turned the camera");
        // Against a camera that answers as the game's does (180 degrees a second at full tilt, the cube of the tilt
        // above the floor below that, a tenth of a second behind): from 170 degrees off it is there within two
        // seconds, in one turn, and stays.
        CameraLook steer;
        float yaw=0, rate=0, worst=0;
        const float dt=1.f/90;
        for(int i=0;i<270;++i) {
            const float stick=steer.update(at(170),at(yaw),true);
            const float over=std::max(0.f,(std::abs(stick)-CameraLook::floor)/(1-CameraLook::floor));
            const float wanted=std::copysign(CameraLook::fullRate*over*over*over,stick);
            rate+=(wanted-rate)*std::min(1.f,dt/.1f);
            yaw+=rate*dt;
            if(i>=180) worst=std::max(worst,std::abs(steer.error()));
        }
        check(worst<=CameraLook::startDegrees && steer.turns()==1,"the camera did not settle on the look in one turn");
        // The stick is the virtual controller's right one, in VR play alone.
        using namespace game_pad;
        Walk walk;
        walk.look=-.75f;
        XrFrame f;
        check(fromControllers(f,Mapping::gameplay,walk).thumbRX==-24575 && !fromControllers(f,Mapping::gameplay,walk).thumbRY,
              "the look is not the right stick's X");
        check(!fromControllers(f,Mapping::menus,walk).thumbRX && !fromControllers(f,Mapping::none,walk).thumbRX,
              "the camera was turned on the game screen");
    });
    test("the right thumbstick's click is the game's LB + RB, and not half of clicking both", [] {
        using game_pad::PullPress;
        PullPress press;
        // Held, it holds from the chord's time on, for as long as the stick is down.
        float began = -1;
        for (int i = 0; i < 60; ++i)
            if (press.update(.011f, true, true, false) && began < 0)
                began = i * .011f;
        check(began >= PullPress::chordSeconds - .012f && began <= PullPress::chordSeconds + .012f,
              "a held click began to pull at the wrong time");
        check(press.update(.011f, true, true, false), "a held click let go of the pull");
        check(!press.update(.011f, true, false, false), "the pull outlasted the stick");
        // A short click holds for a moment after it.
        PullPress tap;
        check(!tap.update(.011f, true, true, false) && !tap.update(.011f, true, true, false), "a click pulled at once");
        float held = 0;
        for (int i = 0; i < 60; ++i)
            held += tap.update(.011f, true, false, false) ? .011f : 0;
        check(held >= PullPress::tapSeconds - .012f && held <= PullPress::tapSeconds + .012f,
              "a short click did not hold for its time");
        // Both sticks, either first, switch the screen and pull nothing.
        for (const bool rightFirst : {false, true}) {
            PullPress chord;
            bool any = chord.update(.011f, true, rightFirst, !rightFirst);
            for (int i = 0; i < 30; ++i)
                any = chord.update(.011f, true, true, true) || any;
            for (int i = 0; i < 30; ++i)
                any = chord.update(.011f, true, false, false) || any;
            check(!any, "clicking both sticks pulled");
        }
        // The left stick joining a pull ends it, until the right one is let go.
        PullPress joined;
        for (int i = 0; i < 20; ++i)
            joined.update(.011f, true, true, false);
        check(joined.update(.011f, true, true, false), "a held click did not pull");
        check(!joined.update(.011f, true, true, true) && !joined.update(.011f, true, true, false),
              "a pull went on after the left stick joined");
        // Outside VR play nothing pulls, and a stick held through it waits for a release.
        PullPress screen;
        for (int i = 0; i < 30; ++i)
            check(!screen.update(.011f, false, true, false), "a click on the game screen pulled");
        for (int i = 0; i < 30; ++i)
            check(!screen.update(.011f, true, true, false), "a click held into play pulled");
        check(!screen.update(.011f, true, false, false), "letting go of a click held into play pulled");
        for (int i = 0; i < 20; ++i)
            screen.update(.011f, true, true, false);
        check(screen.update(.011f, true, true, false), "a new click after the screen did not pull");
    });
    test("the right stick pushed down heals and pushed up is the game's LT + RT; a turn, a diagonal and a flick are neither", [] {
        using game_pad::StickPush;
        using Way = StickPush::Way;
        const float dt = .011f;
        // Frames of the stick held at (x, y); what the last one gave.
        const auto hold = [&](StickPush& s, float x, float y, int frames = 30, bool play = true) {
            Way way = Way::none;
            for (int i = 0; i < frames; ++i)
                way = s.update(dt, play, x, y);
            return way;
        };
        StickPush push;
        // Held down, it heals from holdSeconds on, for as long as the stick stays down.
        float began = -1;
        for (int i = 0; i < 40; ++i)
            if (push.update(dt, true, 0, -1) == Way::down && began < 0)
                began = (i + 1) * dt;
        check(began >= StickPush::holdSeconds - dt && began <= StickPush::holdSeconds + dt,
              "a push down healed at the wrong time");
        check(hold(push, .2f, -.6f) == Way::down, "a push eased a little let go");
        check(hold(push, 0, 0, 1) == Way::none, "the heal outlasted the stick");
        // Up is the zip, a little off the line too.
        check(hold(push, -.3f, .9f) == Way::up, "a push up is not the zip");
        hold(push, 0, 0, 1);
        // A flick through the line is neither.
        check(hold(push, 0, -1, 5) == Way::none && hold(push, 0, 0, 1) == Way::none, "a flick healed");
        // The stick turns the player with its left and right; a turn rolled round the rim pushes nothing.
        check(hold(push, 1, 0) == Way::none && hold(push, .3f, .95f) == Way::none && hold(push, 0, -1) == Way::none,
              "a turn rolled round to the top or the bottom pushed");
        hold(push, 0, 0, 1);
        check(hold(push, .6f, -.75f) == Way::none && hold(push, 0, -1) == Way::none, "a diagonal healed");
        hold(push, 0, 0, 1);
        // From down straight over to up is no zip, nor is a push slid aside into a turn and back.
        check(hold(push, 0, -1) == Way::down && hold(push, 0, 1) == Way::none, "a heal swung over into a zip");
        hold(push, 0, 0, 1);
        check(hold(push, 0, 1) == Way::up, "a new push from rest did not zip");
        check(hold(push, .9f, .3f) == Way::none && hold(push, 0, 1) == Way::none, "a push slid into a turn came back");
        // Outside VR play nothing is pushed, and a stick held through it waits for a release.
        StickPush screen;
        check(hold(screen, 0, -1, 30, false) == Way::none, "the game screen's right stick healed");
        check(hold(screen, 0, -1) == Way::none, "a stick held into play healed");
        hold(screen, 0, 0, 1);
        check(hold(screen, 0, -1) == Way::down, "a new push after the screen did not heal");
        // A push under way keeps through a moment out of play (a long frame) and goes on; a longer one spends it.
        StickPush blip;
        hold(blip, 0, -1, 8);
        check(hold(blip, 0, -1, 10, false) == Way::none, "a heal went out out of play");
        check(hold(blip, 0, -1, 10) == Way::down, "a moment out of play spent the heal");
        hold(blip, 0, 0, 1);
        hold(blip, 0, -1, 8);
        check(hold(blip, 0, -1, 40, false) == Way::none && hold(blip, 0, -1) == Way::none,
              "a push held through a long time out of play healed");
        // Bad readings and bad timing push nothing.
        StickPush broken;
        const float bad = std::numeric_limits<float>::quiet_NaN();
        check(hold(broken, bad, -1) == Way::none && hold(broken, 0, bad) == Way::none, "a bad stick reading pushed");
        for (int i = 0; i < 30; ++i)
            check(broken.update(bad, true, 0, -1) == Way::none, "a bad frame time pushed");
    });
    test("a zip held into the game's aim mode keeps its LT down alone and zips again as it is let go", [] {
        using game_pad::ZipPress;
        const float dt = .011f;
        // Frames of the stick held up or not, the game's time slow or not; the keys of the last.
        const auto run = [&](ZipPress& z, bool up, bool slow, int frames, bool play = true) {
            ZipPress::Keys keys;
            for (int i = 0; i < frames; ++i)
                keys = z.update(dt, up, play, slow);
            return keys;
        };
        // Frames of LT + RT, and of LT alone, while the stick is let go for `frames`.
        const auto letGo = [&](ZipPress& z, int frames, int& zip, int& aim) {
            zip = aim = 0;
            for (int i = 0; i < frames; ++i) {
                const auto keys = z.update(dt, false, true, true);
                zip += keys.zip;
                aim += keys.aim && !keys.zip;
            }
        };
        // A push the game zips from at once (its time never slows): LT + RT for as long as it is held,
        // nothing after.
        ZipPress quick;
        auto keys = run(quick, true, false, 90);
        check(keys.zip && !keys.aim, "a held zip let its triggers go");
        keys = run(quick, false, false, 30);
        check(!keys.zip && !keys.aim && quick.releases() == 0 && quick.aims() == 0,
              "letting go of a zip held in normal time zipped again");
        // The game slows its time (its aim mode): LT + RT stay down pressSeconds from the push, then LT
        // alone.
        ZipPress aimed;
        keys = run(aimed, true, true, 5);
        check(keys.zip && !keys.aim, "the zip came up before the game had it pressSeconds");
        keys = run(aimed, true, true, 40);
        check(!keys.zip && keys.aim && aimed.aims() == 1, "the aim mode did not keep LT down alone");
        // The game's time back to normal (its slow motion lasts 3 s at most) keeps the aim.
        keys = run(aimed, true, false, 300);
        check(!keys.zip && keys.aim, "the aim ended with the game's slow motion");
        // Let go: LT + RT once more, for pressSeconds; then nothing.
        int zip{}, aim{};
        letGo(aimed, 90, zip, aim);
        check(aimed.releases() == 1 && zip * dt >= ZipPress::pressSeconds - dt &&
                  zip * dt <= ZipPress::pressSeconds + 2 * dt && aim == 0,
              "letting go of the aim did not zip once");
        // RT up only a moment as the stick is let go: the zip waits until it has been up gapSeconds, LT down.
        ZipPress hurried;
        keys = run(hurried, true, true, 1);
        while (!keys.aim)
            keys = hurried.update(dt, true, true, true);
        letGo(hurried, 90, zip, aim);
        check(hurried.releases() == 1 && zip > 0 && aim >= static_cast<int>(ZipPress::gapSeconds / dt) - 1,
              "the zip let go of went out without RT up first");
        // A hold let go before the game's time slowed is the zip it was.
        ZipPress early;
        run(early, true, false, 20);
        run(early, false, true, 30);
        check(early.releases() == 0 && early.aims() == 0,
              "a zip let go in normal time aimed or zipped again");
        // Out of play nothing goes out. An aim keeps through a moment of it; a longer time drops it, and
        // letting go then sends nothing.
        ZipPress blip;
        run(blip, true, true, 40);
        keys = run(blip, true, true, 5, false);
        check(!keys.zip && !keys.aim, "keys went out out of play");
        keys = run(blip, true, true, 1);
        check(keys.aim && !keys.zip, "a moment out of play dropped the aim");
        run(blip, true, true, 40, false);
        letGo(blip, 60, zip, aim);
        check(zip == 0 && blip.releases() == 0, "an aim dropped out of play zipped as the stick was let go");
        // A press let go of goes no further once play ends.
        ZipPress menu;
        run(menu, true, true, 40);
        menu.update(dt, false, true, true);
        keys = run(menu, false, true, 3, false);
        check(!keys.zip && run(menu, false, true, 30).zip == false, "a zip let go of went on into a menu");
        // Bad frame times count no time: the zip never gives way to the aim.
        ZipPress broken;
        for (int i = 0; i < 60; ++i)
            keys = broken.update(std::numeric_limits<float>::quiet_NaN(), true, true, true);
        check(keys.zip && !keys.aim, "a bad frame time aimed");
        // The aim is the game's LT alone; the zip its LT + RT.
        game_pad::Walk walk;
        walk.aim = true;
        const auto alone = game_pad::fromControllers(XrFrame{}, game_pad::Mapping::gameplay, walk);
        check(alone.leftTrigger == 255 && alone.rightTrigger == 0 && alone.buttons == 0,
              "the aim is not LT alone");
        walk.aim = false;
        walk.zip = true;
        const auto both = game_pad::fromControllers(XrFrame{}, game_pad::Mapping::gameplay, walk);
        check(both.leftTrigger == 255 && both.rightTrigger == 255, "the zip is not LT + RT");
    });
    test("OpenXR Toolkit is switched off by the variable its own manifest names; other layers stay on", [] {
        using namespace xr_layers;
        // The Toolkit's manifest as its repository has it (mbucchia/OpenXR-Toolkit, October 12).
        const std::string toolkit = R"({
  "file_format_version" : "1.0.0",
  "api_layer": {
    "name": "XR_APILAYER_MBUCCHIA_toolkit",
    "library_path": ".\\XR_APILAYER_MBUCCHIA_toolkit.dll",
    "api_version": "1.0",
    "implementation_version": "1",
    "description": "OpenXR toolkit API layer",
    "functions": {
      "xrNegotiateLoaderApiLayerInterface": "xrNegotiateLoaderApiLayerInterface"
    },
    "disable_environment": "DISABLE_XR_APILAYER_MBUCCHIA_toolkit"
  }
})";
        const auto off = switchOff(toolkit);
        check(off.variable == "DISABLE_XR_APILAYER_MBUCCHIA_toolkit" && off.name == "OpenXR Toolkit",
              "the Toolkit's own switch was not found");
        check(member(toolkit, "library_path") == ".\\XR_APILAYER_MBUCCHIA_toolkit.dll", "an escaped path was misread");
        // Other layers stay on: Virtual Desktop's and ReShade's as a PC has them, and one whose text
        // only mentions the Toolkit.
        const std::string reshade = "{\n\t\"file_format_version\": \"1.0.0\",\n\t\"api_layer\": {\n"
                                    "\t\t\"name\": \"XR_APILAYER_reshade\",\n"
                                    "\t\t\"library_path\": \".\\\\ReShade64.dll\",\n"
                                    "\t\t\"disable_environment\": \"DISABLE_XR_APILAYER_reshade_1\"\n\t}\n}";
        check(switchOff(reshade).variable.empty(), "ReShade's layer was switched off");
        check(switchOff(R"({"api_layer": {"name": "XR_APILAYER_VIRTUALDESKTOP_oculus_compatibility",
            "disable_environment": "DISABLE_XR_APILAYER_VIRTUALDESKTOP_OCULUS_COMPATIBILITY"}})").variable.empty(),
              "Virtual Desktop's layer was switched off");
        check(switchOff(R"({"api_layer":{"name":"XR_APILAYER_NOVENDOR_other","description":"XR_APILAYER_MBUCCHIA_toolkit",
                            "disable_environment":"DISABLE_OTHER"}})").variable.empty(),
              "a layer Spidy runs with was switched off");
        // A layer is switched off for being on the list, by the variable it names itself.
        const Layer listed[] = {{"XR_APILAYER_reshade", "ReShade"}};
        const auto other = switchOff(reshade, listed);
        check(other.variable == "DISABLE_XR_APILAYER_reshade_1" && other.name == "ReShade" &&
                  switchOff(toolkit, listed).variable.empty(),
              "the list did not decide which layer is switched off");
        // The manifest's own word counts: compact text, another case, another variable.
        check(switchOff(R"({"api_layer":{"disable_environment":"OXRTK_OFF","name":"xr_apilayer_mbucchia_TOOLKIT"}})")
                      .variable == "OXRTK_OFF",
              "the variable was not taken from the manifest");
        // No variable, or one no process could set: nothing is set.
        check(switchOff(R"({"api_layer":{"name":"XR_APILAYER_MBUCCHIA_toolkit"}})").variable.empty(),
              "a manifest without a switch gave one");
        check(switchOff(R"({"api_layer":{"name":"XR_APILAYER_MBUCCHIA_toolkit","disable_environment":"A=B C"}})")
                  .variable.empty(),
              "a variable no process could set was taken");
        // A key's text as a value is no key; a cut-off file gives nothing.
        check(member(R"({"description":"name","name":"x"})", "name") == "x", "a value was read as a key");
        check(member(R"({"name":"cut)", "name").empty() && member("", "name").empty(), "a cut-off manifest gave a value");
    });
    test("both web buttons on a thing no web holds on, and a yank, are the game's LB + RB", [] {
        // A door 3 m ahead (-Z); the hands a shoulder's width apart, each pointing at it.
        const Vec3 door[2] = {{-.4f, 1.2f, -3}, {.4f, 1.2f, -3}};
        const Vec3 rest[2] = {{-.25f, -.3f, -.4f}, {.25f, -.3f, -.4f}}; // from the head
        const Vec3 head{0, 1.7f, 0};
        const Quat level{};
        struct Step {
            bool held[2];
            bool blocked[2];
            float back[2]; // each hand drawn this far back (+Z) from its rest
        };
        const auto hands = [&](const Step& s) {
            std::array<WebPullHand, 2> out;
            for (int i = 0; i < 2; ++i) {
                const Vec3 grip = rest[i] + Vec3{0, 0, s.back[i]};
                out[i] = {s.held[i], s.blocked[i], door[i], head + grip, grip};
            }
            return out;
        };
        constexpr float dt = 1.f / 90;
        // A yank: 0.3 m back at 3 m/s.
        const auto yank = [&](WebPull& pull, bool ready, bool left, bool right, bool blocked = true) {
            bool any{};
            for (int i = 1; i <= 9; ++i)
                any = pull.update(dt, ready, level,
                                  hands({{true, true},
                                         {blocked, blocked},
                                         {left ? i * 3 * dt : 0, right ? i * 3 * dt : 0}})) ||
                      any;
            return any;
        };
        const Step open{{false, false}, {true, true}, {0, 0}}, pressed{{true, true}, {true, true}, {0, 0}};
        const Step drawn{{true, true}, {true, true}, {.3f, .3f}};
        WebPull pull;
        check(!pull.update(dt, true, level, hands(open)) && !pull.aimed(), "open hands aimed");
        check(!pull.update(dt, true, level, hands(pressed)) && pull.aimed(), "both web buttons on a door did not aim");
        for (int i = 0; i < 30; ++i)
            check(!pull.update(dt, true, level, hands(pressed)), "holding both web buttons pulled without a yank");
        check(yank(pull, true, true, true) && pull.pulls() == 1, "a yank of both hands did not pull");
        // LB + RB stay down while both web buttons do, and let go with them.
        for (int i = 0; i < 60; ++i)
            check(pull.update(dt, true, level, hands(drawn)), "the pull ended while both web buttons were down");
        check(!pull.update(dt, true, level, hands({{true, false}, {true, true}, {.3f, .3f}})),
              "the pull outlasted a web button");
        check(pull.pulls() == 1, "one yank pulled twice");
        // A yank that lets go of the grips at once still holds the buttons a moment.
        WebPull quick;
        quick.update(dt, true, level, hands(open));
        quick.update(dt, true, level, hands(pressed));
        check(yank(quick, true, true, false), "one hand's yank did not pull");
        float held = 0;
        for (int i = 0; i < 60; ++i)
            held += quick.update(dt, true, level, hands(open)) ? dt : 0;
        check(held > .15f && held < .32f, "a short pull did not hold for its time");
        // At most for so long, both buttons down or not.
        WebPull stuck;
        stuck.update(dt, true, level, hands(open));
        stuck.update(dt, true, level, hands(pressed));
        yank(stuck, true, true, true);
        int frames = 0;
        while (frames < 2000 && stuck.update(dt, true, level, hands(drawn)))
            ++frames;
        check(frames * dt > 3 && frames * dt < 4.1f, "a held pull did not end");
        // Where a web would hold (no red cross), or with one hand only, a yank is the swing's.
        WebPull holds;
        holds.update(dt, true, level, hands(open));
        holds.update(dt, true, level, hands({{true, true}, {false, false}, {0, 0}}));
        check(!holds.aimed() && !yank(holds, true, true, true, false), "a web that holds was pulled as a door");
        WebPull single;
        single.update(dt, true, level, hands(open));
        single.update(dt, true, level, hands({{true, false}, {true, true}, {0, 0}}));
        bool any{};
        for (int i = 1; i <= 9; ++i)
            any = single.update(dt, true, level, hands({{true, false}, {true, true}, {i * 3 * dt, 0}})) || any;
        check(!any, "one web button pulled");
        // A slow draw back is no yank, nor is a push toward the door.
        WebPull slow;
        slow.update(dt, true, level, hands(open));
        slow.update(dt, true, level, hands(pressed));
        any = false;
        for (int i = 1; i <= 90; ++i)
            any = slow.update(dt, true, level, hands({{true, true}, {true, true}, {i * .4f * dt, i * .4f * dt}})) || any;
        for (int i = 1; i <= 9; ++i)
            any = slow.update(dt, true, level,
                              hands({{true, true}, {true, true}, {.4f - i * 3 * dt, .4f - i * 3 * dt}})) ||
                  any;
        check(!any, "a slow draw or a push pulled");
        // Not while the game does not have the player on the ground, and web
        // buttons held from then on aim at nothing.
        WebPull away;
        away.update(dt, false, level, hands(open));
        away.update(dt, false, level, hands(pressed));
        check(!yank(away, false, true, true), "a yank pulled while the game did not have the player");
        check(!away.update(dt, true, level, hands(pressed)) && !away.aimed() && !yank(away, true, true, true),
              "web buttons held from a swing pulled on landing");
        // Two hands on things far apart, or on one out of reach, pull nothing.
        WebPull apart;
        apart.update(dt, true, level, hands(open));
        auto wide = hands(pressed);
        wide[1].point = {9, 1.2f, -3};
        apart.update(dt, true, level, wide);
        check(!apart.aimed(), "two things were one");
        WebPull far;
        far.update(dt, true, level, hands(open));
        auto distant = hands(pressed);
        distant[0].point = {-.4f, 1.2f, -40};
        distant[1].point = {.4f, 1.2f, -40};
        far.update(dt, true, level, distant);
        check(!far.aimed(), "a thing out of reach was aimed at");
        // The tracking space turned a quarter: the same yank, away from the door there.
        WebPull turned;
        const Quat quarter = Quat::yaw(1.5707963f);
        const auto turnedHands = [&](const Step& s) {
            auto out = hands(s);
            for (int i = 0; i < 2; ++i) {
                out[i].point = quarter.rotate(door[i]);
                out[i].position = head + quarter.rotate(out[i].motion);
            }
            return out;
        };
        turned.update(dt, true, quarter, turnedHands(open));
        turned.update(dt, true, quarter, turnedHands(pressed));
        any = false;
        for (int i = 1; i <= 9; ++i)
            any = turned.update(dt, true, quarter,
                                turnedHands({{true, true}, {true, true}, {i * 3 * dt, i * 3 * dt}})) ||
                  any;
        check(any, "a yank in a turned tracking space did not pull");
    });
    test("the render allocator gets a larger ring only where the game creates it", [] {
        using namespace native_render_memory;
        // As the game's creation (1872d90) leaves it: a 128 MB ring, all of it
        // free for the first frame, nothing counted.
        const uint64_t ring = 0x185b4180000;
        Fields made;
        made.ring = made.first = ring;
        made.reserved = made.committed = made.firstSize = 128u << 20;
        made.flags[3] = 1;
        check(usable(made) && untouched(made), "a newly created allocator rejected");
        const uint64_t larger = 0x20000000000;
        const uint32_t bytes = 512u << 20;
        const auto next = replaced(made, larger, bytes);
        check(usable(next) && untouched(next), "the replaced allocator is not as new");
        check(next.ring == larger && next.first == larger && next.reserved == bytes && next.committed == bytes &&
                  next.firstSize == bytes,
              "ring not replaced");
        check(next.flags[3] == 1, "the allocator's flags changed");
        // Mid-session (October 5): the next frame free from 27 MB to the last
        // page and from the ring's start up to the frame that just ended.
        // Render commands hold 32-bit offsets from the ring's base into those
        // frames, so this ring must stay where it is.
        auto running = made;
        running.first = ring + 0x1b09300;
        running.firstSize = 0x064f5d00;
        running.second = ring;
        running.secondSize = 0x0152f200;
        running.lastFrame = 6u << 20;
        running.worstPair = 14u << 20;
        check(usable(running), "the game's ring in use rejected");
        check(!untouched(running), "an allocator with frames in it treated as new");
        for (int broken = 0; broken < 6; ++broken) {
            auto bad = running;
            if (broken == 0)
                bad.ring = 0;
            if (broken == 1)
                bad.committed = bad.reserved + 1;
            if (broken == 2)
                bad.first = ring - 0x100;
            if (broken == 3)
                bad.secondSize = bad.committed + 1;
            if (broken == 4)
                bad.reserved = bad.committed = 3u << 30;
            if (broken == 5)
                bad.reserved = bad.committed = 1u << 20;
            check(!usable(bad) && !untouched(bad), "a ring this module does not understand was accepted");
        }
        auto used = made;
        used.firstUsed = 64;
        check(!untouched(used), "an allocator that has handed out memory treated as new");
        // What a session finds when it starts: no ring yet, the game's own, or
        // the one an earlier session of the same game installed.
        check(ringStatus(Fields{}, 0) == 1 && ringStatus(Fields{}, larger) == 1,
              "a ring reported before the game made one");
        check(ringStatus(running, 0) == 2 && ringStatus(running, larger) == 2,
              "the game's own ring not reported as such");
        auto later = next;
        later.first = larger + 0x1b09300;
        later.lastFrame = 60u << 20;
        check(ringStatus(later, larger) == 3, "a later session does not recognize the installed ring");
    });
    test("delayed native images retain their own tracking and overlay poses", [] {
        NativeEyeHistory history;
        auto tracked=trackedFrame();
        tracked.predictedDisplayTime=1000000000;
        GameTrackingRig rig;
        NativeEyeFrame rendered;
        rendered.serial=20;
        rendered.motion=rig.update(tracked,{10,20,30},{0,0,1},true);
        rendered.trackingEyes=tracked.eyes;
        rendered.webs[0].attached=1;
        rendered.webs[0].anchor={50,60,70};
        history.remember(rendered);
        auto newer=rendered;
        newer.serial=24;
        newer.motion.predictedDisplayTime+=10000000;
        newer.trackingEyes[0].pose.position.x+=.5f;
        newer.motion.eyes[0][12]+=10;
        newer.motion.hands[0].position.x+=10;
        newer.webs[0].attached=0;
        history.remember(newer);
        const auto* old=history.find(20,newer.motion.predictedDisplayTime);
        check(old,"rendered serial lost after a newer tracking sample");
        near(old->trackingEyes[0].pose.position.x,rendered.trackingEyes[0].pose.position.x);
        near(old->trackingEyes[1].fov.up,rendered.trackingEyes[1].fov.up);
        near(old->motion.eyes[0][12],rendered.motion.eyes[0][12]);
        near(old->motion.hands[0].position.x,rendered.motion.hands[0].position.x);
        check(old->webs[0].attached,"web overlay came from a newer frame");
        near(old->webs[0].anchor.z,70);
        check(history.find(20,newer.motion.predictedDisplayTime+10000000)==old,
              "repeated projection lost its original render pose");
    });
    test("native image history rejects stale, overwritten and recentered poses", [] {
        NativeEyeHistory history;
        NativeEyeFrame frame;
        frame.serial=7;
        frame.motion.predictedDisplayTime=1000000000;
        history.remember(frame);
        check(!history.find(0,1000000000),"uncontrolled native image accepted");
        check(!history.find(7,999999999),"future image accepted");
        // An image stays on show for a second while no newer one arrives.
        check(history.find(7,2000000000),"valid image age boundary rejected");
        check(!history.find(7,2000000001),"stale image retained");
        for(uint64_t i=8;i<=263;++i) {
            frame.serial=i;
            history.remember(frame);
        }
        check(!history.find(7,1000000000),"overwritten pose silently used for an old image");
        check(history.find(263,1000000000),"new ring entry lost");
        history.clear();
        check(!history.find(263,1000000000),"pre-recenter render pose retained");
    });
    test("eye snapshots are routine every few seconds and sooner on a fast held web", [] {
        EyeSnapshotSchedule s;
        check(s.due(0,0,false),"first snapshot not taken at tick zero");
        check(!s.due(4999,0,false)&&s.due(5000,0,false),"routine interval is not five seconds");
        check(!s.due(6400,32,true),"event snapshot ignored its own interval");
        check(s.due(6500,32,true),"fast held web did not bring the snapshot forward");
        check(!s.due(8100,14,true)&&!s.due(8100,32,false),"slow swing or free flight counted as an event");
        check(!s.due(8100,std::numeric_limits<float>::quiet_NaN(),true),"invalid speed counted as an event");
        check(s.due(11500,0,false),"routine snapshots stopped after an event");
        check(sizeof(EyeSnapshot)==112,"snapshot header changed size");
    });
    test("eye pixels follow the overlay projection and reject points behind the eye", [] {
        const Pose eye{{10,20,30},Quat::yaw(.5f)};
        const auto vp=multiply(projection(-.9f,.7f,-.8f,.75f,.03f,200.f),viewMatrix(eye));
        float x{},y{};
        // A point on the left and upper edges of the lens lands on the image corner.
        const Vec3 corner=eye.position+eye.orientation.rotate({std::tan(-.9f)*4,std::tan(.75f)*4,-4});
        check(eyePixel(vp,corner,3072,3264,x,y),"visible point rejected");
        near(x,0,.05f);near(y,0,.05f);
        const Vec3 ahead=eye.position+eye.orientation.rotate({0,0,-2});
        check(eyePixel(vp,ahead,3072,3264,x,y),"point straight ahead rejected");
        near(x,3072*std::tan(.9f)/(std::tan(.9f)+std::tan(.7f)),.05f);
        near(y,3264*std::tan(.75f)/(std::tan(.75f)+std::tan(.8f)),.05f);
        check(!eyePixel(vp,eye.position+eye.orientation.rotate({0,0,1}),3072,3264,x,y),"point behind the eye accepted");
        check(!eyePixel(vp,{std::numeric_limits<float>::quiet_NaN(),0,0},3072,3264,x,y),"invalid point accepted");
        // The native eye pose converts to the same camera the overlay uses.
        const Mat4 basis{1,0,0,0,0,-1,0,0,0,0,-1,0,0,0,0,1};
        const auto native=multiply(projection(-.9f,.7f,-.8f,.75f,.03f,200.f),
                                   native_view::view(native_view::relativePose(basis,eye)));
        float nx{},ny{};
        check(eyePixel(native,ahead,3072,3264,nx,ny),"native camera rejected the point");
        near(nx,x,.05f);near(ny,y,.05f);
    });
    test("native swing input rejects malformed tracking and keeps hand identity", [] {
        game_swing::Command c; c.focused=1; c.serial=1;
        c.hands[0].tracked=1; c.hands[0].aim.position={1,2,3}; c.hands[0].grip=1;
        c.hands[1].tracked=0; c.hands[1].aim.position={4,5,6};
        check(game_swing::valid(c),"valid native hand sample rejected");
        const auto in=game_swing::input(c);
        check(in.hands[0].tracked && !in.hands[1].tracked,"hand identity lost");
        near(in.hands[1].aim.position.z,6);
        auto bad=c; bad.hands[0].aim.orientation.w=2;
        check(!game_swing::valid(bad),"scaled hand orientation accepted");
        bad=c; bad.leaseMs=251; check(!game_swing::valid(bad),"unbounded input lease accepted");
        bad=c; bad.move={2,0,0}; check(!game_swing::valid(bad),"unbounded steering accepted");
        bad=c; bad.sampleSeconds=0; check(!game_swing::valid(bad),"zero input interval accepted");
        bad=c; bad.sampleSeconds=.101f; check(!game_swing::valid(bad),"stale input interval accepted");
        bad=c; bad.sampleSeconds=std::numeric_limits<float>::quiet_NaN();
        check(!game_swing::valid(bad),"NaN input interval accepted");
    });
    test("a web aimed at a prop grabs it and the swing never sees that press", [] {
        TestWorld world;world.anchor={0,20,-30};
        GrabTargets targets;targets.add(7,{0,1,-8});
        WebGrab grab;Swing swing(inert());
        Body player{{0,1,0},{},true};
        const auto in=forward(),forSwing=grab.claim(1.f/90,in,world,targets,player);
        check(grab.grabs()[0].phase==GrabPhase::Tethered&&grab.grabs()[0].target==7,"prop not grabbed");
        near(forSwing.hands[0].grip,0);
        swing.predictNativeStep(.01f,forSwing,world,player);
        check(!swing.webs()[0].attached,"the same press also shot a swing web");
        check(events(grab,GrabEventKind::Grab)==1,"no grab event");
    });
    test("a prop behind a wall cannot be grabbed and the press swings instead", [] {
        struct Wall : TestWorld {
            std::optional<RayHit> raycast(Vec3 o,Vec3 d,float distance) const override {
                const float t=(o.z+5)/-d.z; // a wall across z=-5
                if(d.z<0&&t<=distance)return RayHit{o+d*t,{0,0,1},3,true};
                return {};
            }
            bool exists(std::uint64_t id) const override {return id==3;}
        } world;
        GrabTargets targets;targets.add(7,{0,1,-8});
        WebGrab grab;Swing swing(inert());Body player{{0,1,0},{},true};
        const auto forSwing=grab.claim(1.f/90,forward(),world,targets,player);
        check(grab.grabs()[0].phase==GrabPhase::None,"grabbed through a wall");
        swing.predictNativeStep(.01f,forSwing,world,player);
        check(swing.webs()[0].attached,"the press was lost to the swing");
    });
    test("a press the swing keeps catches nothing, whatever the hand points at", [] {
        TestWorld world;world.anchor={0,20,-30};
        GrabTargets targets;targets.add(7,{0,1,-8});
        WebGrab grab;Body player{{0,1,0},{},true};
        const auto in=forward(),forSwing=grab.claim(1.f/90,in,world,targets,player,1);
        check(grab.grabs()[0].phase==GrabPhase::None,"a kept press grabbed the prop");
        near(forSwing.hands[0].grip,1);
        // Held on, it still catches nothing; let go and pressed again, it does.
        grab.claim(1.f/90,in,world,targets,player);
        check(grab.grabs()[0].phase==GrabPhase::None,"a grip held on grabbed later");
        auto open=in;open.hands[0].grip=0;grab.claim(1.f/90,open,world,targets,player);
        grab.claim(1.f/90,in,world,targets,player);
        check(grab.grabs()[0].phase==GrabPhase::Tethered,"the next press did not grab");
    });
    test("a slack web leaves its target alone; a taut one tows it after the hand", [] {
        TestWorld world;world.enabled=false;
        GrabTargets targets;targets.add(7,{0,1,-8});
        WebGrab grab;Body player{{0,1,0},{},true};
        auto in=forward();in.hands[0].trigger=0;
        grab.claim(1.f/90,in,world,targets,player);
        std::vector<TargetCommand> out;
        for(int i=0;i<12;++i)grab.step(1.f/120,world,targets,out);
        check(out.empty(),"taking hold already pulled the target");
        // Walk the hand back two metres over a second: the web tows the target.
        for(int i=1;i<=90;++i) {
            in.hands[0].aim.position={0,1,2.f*i/90};
            grab.claim(1.f/90,in,world,targets,player);
            for(int s=0;s<4;++s){out.clear();grab.step(1.f/360,world,targets,out);targets.apply(1.f/360,out);}
        }
        const auto t=*targets.find(7);
        check(t.position.z>-6.6f,"the target did not follow the hand");
        check(length(t.position-in.hands[0].aim.position)<8.6f,"the web stretched without limit");
        check(grab.grabs()[0].phase==GrabPhase::Tethered,"towing changed the grab");
    });
    test("trigger reels the target in to the hand and catches it", [] {
        TestWorld world;world.enabled=false;
        GrabTargets targets;targets.add(7,{0,1,-12});
        WebGrab grab;Body player{{0,1,0},{},true};
        auto in=forward();in.hands[0].trigger=0;
        grab.claim(1.f/90,in,world,targets,player);
        in.hands[0].trigger=1;
        bool caught{};
        for(int frame=0;frame<180&&!caught;++frame) {
            grab.claim(1.f/90,in,world,targets,player);
            caught|=events(grab,GrabEventKind::Catch)>0;
            std::vector<TargetCommand> out;
            for(int s=0;s<4;++s){out.clear();grab.step(1.f/360,world,targets,out);targets.apply(1.f/360,out);caught|=events(grab,GrabEventKind::Catch)>0;}
        }
        check(caught&&grab.grabs()[0].phase==GrabPhase::Held,"reeling never caught the target");
        for(int i=0;i<450;++i)settle(grab,targets,world,player,in,1.f/90);
        // Held, it hangs from the wrist on its short web, below the hand.
        const Vec3 below={0,1-(grab.config().holdDistance+.4f),0};
        check(length(targets.find(7)->position-below)<.1f,"a held target did not hang below the hand");
    });
    test("a reeled target on the ground is caught at the hand instead of sliding past it", [] {
        // October 6: with the web wound on to the end, a can dragged in at
        // 12 m/s slid under the hand and swung up behind it.
        TestWorld world;world.enabled=false;
        GrabTargets targets;targets.ground=true;targets.add(7,{0,.45f,-12},30,.45f);
        WebGrab grab;Body player{{0,1,0},{},true};
        Input in;in.hands[0]={{{0,1.3f,0},Quat{std::sin(-.035f),0,0,std::cos(-.035f)}},{0,0,0},true,0,1};
        grab.claim(1.f/90,in,world,targets,player);
        check(grab.grabs()[0].phase==GrabPhase::Tethered,"the can was not webbed");
        in.hands[0].trigger=1;
        // Hand samples at 90 Hz, physics at 30 Hz, as in the game in VR.
        float behind=-1e9f,fastest=0,caughtAt=-1;
        std::vector<TargetCommand> out;
        for(int frame=1;frame<=360;++frame) {
            grab.claim(1.f/90,in,world,targets,player);
            if(frame%3)continue;
            out.clear();grab.step(1.f/30,world,targets,out);targets.apply(1.f/30,out);
            const auto t=*targets.find(7);
            behind=std::max(behind,t.position.z);fastest=std::max(fastest,length(t.velocity));
            if(caughtAt<0&&grab.grabs()[0].phase==GrabPhase::Held)caughtAt=length(t.velocity);
        }
        check(caughtAt>=0&&caughtAt<=grab.config().catchSpeed+.5f,"the can was not caught, or caught fast");
        check(fastest>8,"the reel did not drag the can in");
        check(behind<.6f,"the can slid past the hand toward the player");
    });
    test("the zip gesture yanks a target to the hand without overshooting it", [] {
        TestWorld world;world.enabled=false;
        GrabTargets targets;targets.add(7,{0,1,-20});
        WebGrab grab;Body player{{0,1,0},{},true};
        auto in=forward();in.hands[0].trigger=0;
        grab.claim(1.f/90,in,world,targets,player);
        // Pull the hand 0.3 m back toward the body in 0.1 s (head-relative).
        for(int i=1;i<=9;++i){in.hands[0].gripRelativeToHead={0,0,.033f*i};grab.claim(1.f/90,in,world,targets,player);if(events(grab,GrabEventKind::Yank))break;}
        check(grab.grabs()[0].phase==GrabPhase::Yanked,"the pull did not yank");
        float behind=-1e9f;bool caught{};
        for(int frame=0;frame<300&&!caught;++frame) {
            grab.claim(1.f/90,in,world,targets,player);
            std::vector<TargetCommand> out;
            for(int s=0;s<4;++s){out.clear();grab.step(1.f/360,world,targets,out);targets.apply(1.f/360,out);caught|=events(grab,GrabEventKind::Catch)>0;}
            behind=std::max(behind,targets.find(7)->position.z);
        }
        check(caught&&grab.grabs()[0].phase==GrabPhase::Held,"the yanked target was not caught");
        check(behind<.1f,"the target flew past the hand");
        // Caught, it swings on its web and settles below the hand.
        for(int i=0;i<540;++i)settle(grab,targets,world,player,in,1.f/90);
        const auto t=*targets.find(7);
        check(t.position.y<1-.8f*(grab.config().holdDistance+.4f)&&std::abs(t.position.z)<.35f,"the caught target did not hang below the hand");
    });
    test("a long yank flies all the way to the hand; a snagged one stays on its web", [] {
        // On October 5 a fixed 1.5 s timeout ended a 43 m yank 8.5 m short of the hand.
        TestWorld world;world.enabled=false;
        GrabTargets targets;targets.add(7,{0,1,-45});
        WebGrab grab;Body player{{0,1,0},{},true};
        auto in=forward();in.hands[0].trigger=0;
        grab.claim(1.f/90,in,world,targets,player);
        for(int i=1;i<=9;++i){in.hands[0].gripRelativeToHead={0,0,.033f*i};grab.claim(1.f/90,in,world,targets,player);if(events(grab,GrabEventKind::Yank))break;}
        bool caught{};
        for(int frame=0;frame<540&&!caught;++frame) {
            grab.claim(1.f/90,in,world,targets,player);
            std::vector<TargetCommand> out;
            for(int s=0;s<4;++s){out.clear();grab.step(1.f/360,world,targets,out);targets.apply(1.f/360,out);caught|=events(grab,GrabEventKind::Catch)>0;}
        }
        check(caught,"a 45 m yank did not reach the hand");
        // Pinned in place (snagged): the yank gives up, the web stays.
        GrabTargets pinned;pinned.add(8,{0,1,-20});
        WebGrab other;auto hand=forward();hand.hands[0].trigger=0;
        other.claim(1.f/90,hand,world,pinned,player);
        for(int i=1;i<=9;++i){hand.hands[0].gripRelativeToHead={0,0,.033f*i};other.claim(1.f/90,hand,world,pinned,player);}
        check(other.grabs()[0].phase==GrabPhase::Yanked,"no yank");
        std::vector<TargetCommand> out;
        for(int s=0;s<360;++s){out.clear();other.step(1.f/360,world,pinned,out);} // commands ignored: it cannot move
        check(other.grabs()[0].phase==GrabPhase::Tethered,"a snagged yank never gave up");
    });
    test("a yank launches no faster than its pull allows, a heavy target slower", [] {
        // October 6, 10:16 session: yanks launched props at 26-45 m/s, aimed
        // where the pulling hand would have been after the whole flight.
        auto launch=[](float mass) {
            TestWorld world;world.enabled=false;
            GrabTargets targets;targets.add(7,{0,1,-20},mass);
            WebGrab grab;Body player{{0,1,0},{},true};
            auto in=forward();in.hands[0].trigger=0;
            grab.claim(1.f/90,in,world,targets,player);
            for(int i=1;i<=9;++i){in.hands[0].gripRelativeToHead={0,0,.033f*i};grab.claim(1.f/90,in,world,targets,player);if(events(grab,GrabEventKind::Yank))break;}
            std::vector<TargetCommand> out;grab.step(1.f/120,world,targets,out);
            check(out.size()==1&&out[0].mode==TargetCommand::Mode::Launch&&!out[0].thrown,"the yank did not launch");
            return out[0].velocity;
        };
        const Vec3 light=launch(25),heavy=launch(150);
        const auto& c=WebGrab{}.config();
        check(length(light)<=c.maxYankFlight*c.yankLaunch+.01f&&length(light)>c.minYankFlight,"the yank's launch is out of bounds");
        check(light.z>8,"the yank did not head for the hand");
        check(length(heavy)<length(light)*.6f,"a heavy target was yanked as fast as a light one");
    });
    test("a yank from a perch whose arc falls short is reeled in along the web and caught", [] {
        TestWorld world;world.enabled=false;
        GrabTargets targets;targets.ground=true;targets.add(7,{0,.45f,-30},30,.45f);
        WebGrab grab;Body player{{0,33,0},{},true};
        const Vec3 d=normalized(Vec3{0,.45f-33.3f,-30});const float n=std::sqrt(d.y*d.y+d.x*d.x+(1-d.z)*(1-d.z));
        Input in;in.hands[0]={{{0,33.3f,0},Quat{d.y/n,-d.x/n,0,(1-d.z)/n}},{0,0,0},true,0,1};
        grab.claim(1.f/90,in,world,targets,player);
        check(grab.grabs()[0].phase==GrabPhase::Tethered,"the can was not webbed");
        // The hand comes back and up along the web, 0.3 m in 0.1 s.
        for(int i=1;i<=9;++i){in.hands[0].gripRelativeToHead=d*(-.033f*i);grab.claim(1.f/90,in,world,targets,player);if(events(grab,GrabEventKind::Yank))break;}
        check(grab.grabs()[0].phase==GrabPhase::Yanked,"no yank");
        std::vector<TargetCommand> out;bool caught{};float highest=-1e9f,fastest=0;int frame=0;
        for(;frame<90*8&&!caught;++frame) {
            grab.claim(1.f/90,in,world,targets,player);caught|=events(grab,GrabEventKind::Catch)>0;
            if((frame+1)%3)continue;
            out.clear();grab.step(1.f/30,world,targets,out);targets.apply(1.f/30,out);caught|=events(grab,GrabEventKind::Catch)>0;
            highest=std::max(highest,targets.find(7)->position.y);
            for(const auto& c:out)if(c.mode==TargetCommand::Mode::Launch)fastest=std::max(fastest,length(c.velocity));
        }
        check(caught,"the can never reached the hand on the perch");
        check(highest<33.3f+1.5f,"the can swung up past the hand");
        // Its arc alone could not reach (26-28 m/s needed): the web pulled it up.
        check(fastest<26,"the yank launched the can at the speed the arc needed");
    });
    test("a yank flies an arc, lobbed higher over a railing the lower arc would strike", [] {
        // October 6: a trash can behind a subway railing, flown straight at
        // the hand, stopped against the railing on four yanks.
        struct Railing : TestWorld {
            Vec3 can{0,.5f,-8};
            std::optional<RayHit> raycast(Vec3 o,Vec3 d,float distance) const override {
                std::optional<RayHit> best;float nearest=distance;
                // The railing, 0.8 m high, 1.7 m in front of the can.
                const float lo[3]={-3,0,-6.3f},hi[3]={3,.8f,-6.f},from[3]={o.x,o.y,o.z},along[3]={d.x,d.y,d.z};
                float enter=0,leave=distance;bool inside=true;
                for(int a=0;a<3&&inside;++a){
                    if(std::abs(along[a])<1e-6f){inside=from[a]>=lo[a]&&from[a]<=hi[a];continue;}
                    float t0=(lo[a]-from[a])/along[a],t1=(hi[a]-from[a])/along[a];
                    if(t0>t1)std::swap(t0,t1);
                    enter=std::max(enter,t0);leave=std::min(leave,t1);inside=enter<=leave;
                }
                if(inside){nearest=enter;best=RayHit{o+d*enter,{0,0,1},3,true};}
                // The can's own body, a static body as the game's props are.
                const Vec3 m=o-can;const float b=dot(m,d),c=dot(m,m)-.16f,disc=b*b-c;
                if(disc>=0&&c>0){const float t=-b-std::sqrt(disc);if(t>0&&t<nearest)best=RayHit{o+d*t,normalized(o+d*t-can),77,true};}
                return best;
            }
            bool exists(std::uint64_t id) const override {return id==3||id==77;}
        } world;
        GrabTargets targets;targets.add(7,world.can);targets.owners.push_back({77,7});
        WebGrab grab;Body player{{0,1,0},{},true};
        // The hand at 1.6 m aims over the railing at the can's top.
        const Vec3 d=normalized(Vec3{0,-.75f,-8});const float n=std::sqrt(d.y*d.y+d.x*d.x+(1-d.z)*(1-d.z));
        Input in;in.hands[0]={{{0,1.6f,0},Quat{d.y/n,-d.x/n,0,(1-d.z)/n}},{0,0,0},true,0,1};
        grab.claim(1.f/90,in,world,targets,player);
        check(grab.grabs()[0].phase==GrabPhase::Tethered&&grab.grabs()[0].target==7,"the can was not webbed");
        for(int i=1;i<=9;++i){in.hands[0].gripRelativeToHead={0,0,.033f*i};grab.claim(1.f/90,in,world,targets,player);if(events(grab,GrabEventKind::Yank))break;}
        check(grab.grabs()[0].phase==GrabPhase::Yanked,"no yank");
        bool caught{};float lowest=1e9f,highest=-1e9f;
        for(int frame=0;frame<120&&!caught;++frame) {
            grab.claim(1.f/90,in,world,targets,player);
            std::vector<TargetCommand> out;
            for(int s=0;s<4;++s) {
                out.clear();grab.step(1.f/360,world,targets,out);targets.apply(1.f/360,out);
                caught|=events(grab,GrabEventKind::Catch)>0;
                const auto p=targets.find(7)->position;
                if(p.z>-6.3f&&p.z<-6.f)lowest=std::min(lowest,p.y);
                highest=std::max(highest,p.y);
            }
        }
        check(caught,"the lobbed can was not caught");
        // The can's centre, 0.4 m above its bottom, passes the railing with half a radius to spare.
        check(lowest<1e8f&&lowest>1.f,"the yank flew the can into the railing");
        check(highest>1.6f,"the yank did not arc");
    });
    test("letting go of a yank in flight lets the target fly on; only a held one is thrown", [] {
        TestWorld world;world.enabled=false;
        GrabTargets targets;targets.add(7,{0,1,-20});
        WebGrab grab;Body player{{0,1,0},{},true};
        auto in=forward();in.hands[0].trigger=0;
        grab.claim(1.f/90,in,world,targets,player);
        for(int i=1;i<=9;++i){in.hands[0].gripRelativeToHead={0,0,.033f*i};grab.claim(1.f/90,in,world,targets,player);if(events(grab,GrabEventKind::Yank))break;}
        for(int frame=0;frame<20;++frame)settle(grab,targets,world,player,in,1.f/90);
        check(grab.grabs()[0].phase==GrabPhase::Yanked,"the yank ended early");
        check(length(targets.find(7)->velocity)>8,"the yank did not fly");
        in.hands[0].grip=0;grab.claim(1.f/90,in,world,targets,player);
        check(events(grab,GrabEventKind::Release)==1&&events(grab,GrabEventKind::Throw)==0,"letting go of a yank threw it");
        std::vector<TargetCommand> out;grab.step(1.f/120,world,targets,out);
        check(out.empty(),"the web kept acting after it was let go");
    });
    test("a web's law pulls only when taut, never pushes, and holds up only what it can", [] {
        const Vec3 g{0,-9.81f,0};
        TargetCommand rope{7,TargetKind::Object,TargetCommand::Mode::Rope};
        rope.ropes[0]={{0,1,0},{},5};rope.ropeCount=1;rope.response=30;rope.maxAcceleration=240;
        // Slack, it only falls; coming toward the hand, the web does not push it back.
        auto v=advance(rope,{0,1,-3},{},.01f,g);near(v.y,-.0981f);near(v.z,0);
        v=advance(rope,{0,1,-4},{0,0,8},.01f,g);near(v.z,8);
        // Stretched, it is pulled toward the hand, at most maxAcceleration.
        v=advance(rope,{0,1,-6},{},.01f,g);near(v.z,2.4f,1e-3f);
        auto two=rope;two.ropes[1]={{2,1,0},{},5};two.ropeCount=2;
        check(advance(two,{1,1,-6},{},.01f,g).z>advance(rope,{1,1,-6},{},.01f,g).z+1,"the second web did not pull");
        // Hanging from a web strong enough, a target stays up, sagging under a
        // centimetre; from one too weak for its weight it sinks; two such webs hold it.
        auto hang=[&](TargetCommand c,float seconds) {
            Vec3 p{0,0,0},w{};
            for(int i=0;i<static_cast<int>(seconds*100);++i){w=advance(c,p,w,.01f,g);p+=w*.01f;}
            return p;
        };
        TargetCommand hanging{7,TargetKind::Object,TargetCommand::Mode::Rope};
        hanging.ropes[0]={{0,1,0},{},1};hanging.ropeCount=1;hanging.response=30;hanging.maxAcceleration=80;
        check(hang(hanging,2).y>-.01f,"a web strong enough did not hold its target up");
        hanging.maxAcceleration=8; // 300 kg on 2400 N
        check(hang(hanging,2).y<-1,"a web too weak for its target held it up");
        hanging.ropes[1]=hanging.ropes[0];hanging.ropeCount=2;
        check(hang(hanging,2).y>-.05f,"two webs did not hold up what one could not");
        // Damping slows a swing across the web, not the web's own pull.
        TargetCommand swinging=hanging;swinging.ropeCount=1;swinging.maxAcceleration=80;
        const float free=advance(swinging,{0,0,0},{2,0,0},.01f,g).x;
        swinging.damping=4;
        check(advance(swinging,{0,0,0},{2,0,0},.01f,g).x<free-.05f,"damping did not slow the swing");
        // A brake slows the target toward its velocity at most maxAcceleration; it still falls.
        TargetCommand brake{7,TargetKind::Object,TargetCommand::Mode::Follow};
        brake.velocity={0,0,2};brake.maxAcceleration=80;
        v=advance(brake,{},{0,0,10},.01f,g);near(v.z,9.2f);near(v.y,-.0981f);
        // Turning: a taut web steadies a spin gradually, a launch sets it at once, a web
        // that does not spin never turns it.
        hanging.spins=true;hanging.spin={};hanging.spinAcceleration=20;
        near(advanceSpin(hanging,{0,10,0},.01f).y,9.8f);
        TargetCommand launch{7,TargetKind::Object,TargetCommand::Mode::Launch,true};
        launch.velocity={1,2,3};launch.spins=true;launch.spin={0,5,0};
        near(advance(launch,{},{9,9,9},.01f,g).y,2);near(advanceSpin(launch,{1,1,1},.01f).y,5);
        check(length(advanceSpin(rope,{0,3,0},.01f)-Vec3{0,3,0})<1e-6f,"a web turned a target it only pulls");
    });
    test("a held target hangs below the hand wherever it points, and a wrist flick throws nothing", [] {
        // October 6: "i can hold a bin in the air with a web". The held target
        // followed a point 1.3 m ahead of the hand, held up like on a stick,
        // and a flick of the wrist swung it at 15-18 m/s.
        TestWorld world;world.enabled=false;
        GrabTargets targets;targets.ground=true;targets.add(7,{0,.45f,-6},30,.45f);
        WebGrab grab;Body player{{0,1,0},{},true};
        Input in;in.hands[0]={{{0,1.6f,0},Quat{std::sin(-.096f),0,0,std::cos(-.096f)}},{0,0,0},true,0,1};
        grab.claim(1.f/90,in,world,targets,player);
        in.hands[0].trigger=1;
        for(int i=0;i<270;++i)settle(grab,targets,world,player,in,1.f/90,60);
        check(grab.grabs()[0].phase==GrabPhase::Held,"not held");
        // The hand points straight ahead, level: the target hangs below the wrist, not ahead of it.
        in.hands[0].aim.orientation={};in.hands[0].trigger=0;
        for(int i=0;i<360;++i)settle(grab,targets,world,player,in,1.f/90,60);
        const auto t=*targets.find(7);const float lever=grab.config().holdDistance+.45f;
        check(t.position.y<1.6f-.9f*lever&&std::abs(t.position.z)<.25f,"the held target was not hanging below the hand");
        near(grab.grabs()[0].tension,9.81f*30/grab.config().webForce,.04f);
        // A flick of the wrist, the hand staying put, then letting go: it hardly moves.
        for(int i=1;i<=14;++i){const float a=.8f*i/14;in.hands[0].aim.orientation=Quat{std::sin(a),0,0,std::cos(a)};settle(grab,targets,world,player,in,1.f/90,60);}
        in.hands[0].grip=0;grab.claim(1.f/90,in,world,targets,player);
        check(events(grab,GrabEventKind::Throw)==1,"no throw");
        check(length(grab.events()[0].velocity)<2,"a wrist flick threw what hangs from the web");
    });
    test("an arm swinging a held target throws it, a heavy one slower", [] {
        auto thrown=[](float mass) {
            TestWorld world;world.enabled=false;
            GrabTargets targets;targets.ground=true;targets.add(7,{0,.45f,-6},mass,.45f);
            WebGrab grab;Body player{{0,1,0},{},true};
            Input in;in.hands[0]={{{0,1.6f,0},Quat{std::sin(-.096f),0,0,std::cos(-.096f)}},{0,0,0},true,0,1};
            grab.claim(1.f/90,in,world,targets,player);
            in.hands[0].trigger=1;
            for(int i=0;i<360;++i)settle(grab,targets,world,player,in,1.f/90,60);
            check(grab.grabs()[0].phase==GrabPhase::Held,"not held");
            // An underhand swing: the hand 1.2 m forward and up in a quarter second, then let go.
            const Vec3 from={0,1,.4f},to={0,1.8f,-.6f};
            for(int i=1;i<=27;++i){in.hands[0].aim.position=Vec3{0,1.6f,0}+(from-Vec3{0,1.6f,0})*(i/27.f);settle(grab,targets,world,player,in,1.f/90,60);}
            for(int i=0;i<45;++i)settle(grab,targets,world,player,in,1.f/90,60);
            for(int i=1;i<=22;++i){const float k=i/22.f,s=k*k*(3-2*k);in.hands[0].aim.position=from+(to-from)*s;settle(grab,targets,world,player,in,1.f/90,60);}
            in.hands[0].grip=0;grab.claim(1.f/90,in,world,targets,player);
            check(events(grab,GrabEventKind::Throw)==1,"no throw");
            return length(grab.events()[0].velocity);
        };
        const float light=thrown(30),heavy=thrown(100);
        check(light>6&&light<=WebGrab{}.config().maxThrowSpeed,"the swing threw the can too slow or too fast");
        check(heavy<light*.75f,"a heavy target was thrown as fast as a light one");
    });
    test("one hand letting go of a target both hold leaves it in the other", [] {
        TestWorld world;world.enabled=false;
        GrabTargets targets;targets.add(7,{0,1,-1.3f});
        WebGrab grab;Body player{{0,1,0},{},true};
        auto in=forward();in.hands[1]=in.hands[0];in.hands[1].aim.position.x=.2f;
        in.hands[0].trigger=in.hands[1].trigger=0;grab.claim(1.f/90,in,world,targets,player);
        in.hands[0].trigger=in.hands[1].trigger=1;
        for(int i=0;i<180;++i)settle(grab,targets,world,player,in,1.f/90);
        check(grab.grabs()[0].phase==GrabPhase::Held&&grab.grabs()[1].phase==GrabPhase::Held,"not held by both");
        in.hands[0].grip=0;grab.claim(1.f/90,in,world,targets,player);
        check(events(grab,GrabEventKind::Throw)==0&&events(grab,GrabEventKind::Release)==1,"one hand threw it");
        check(grab.grabs()[1].phase==GrabPhase::Held,"the other hand lost it");
        std::vector<TargetCommand> out;grab.step(1.f/120,world,targets,out);
        check(out.size()==1&&!out[0].thrown&&out[0].mode==TargetCommand::Mode::Rope,"the other hand stopped holding it");
    });
    test("a held target swings alike at any physics rate and settles below the hand", [] {
        auto run=[](int every) {
            TestWorld world;world.enabled=false;
            GrabTargets targets;targets.add(7,{0,1,-1.3f});
            WebGrab grab;Body player{{0,1,0},{},true};
            auto in=forward();in.hands[0].trigger=0;grab.claim(1.f/90,in,world,targets,player);
            in.hands[0].trigger=1;
            // Hand samples at 90 Hz, physics every `every` of them.
            std::vector<TargetCommand> out;int frame=0;
            auto tick=[&]{grab.claim(1.f/90,in,world,targets,player);if(++frame%every==0){out.clear();grab.step(every/90.f,world,targets,out);targets.apply(every/90.f,out);}};
            for(int i=0;i<450;++i)tick();
            check(grab.grabs()[0].phase==GrabPhase::Held,"not held");
            // Half a metre to the side in a third of a second, then still.
            float most=0;
            for(int i=1;i<=720;++i) {
                const float t=std::min(1.f,i/30.f);in.hands[0].aim.position.x=.5f*t*t*(3-2*t);
                tick();most=std::max(most,targets.find(7)->position.x);
            }
            return std::pair{most,targets.find(7)->position};
        };
        const auto [most,settled]=run(1);
        check(most>.55f,"the held target did not swing past the hand that moved it");
        check(std::abs(settled.x-.5f)<.03f&&settled.y<1-.95f*(WebGrab{}.config().holdDistance+.4f),"the target did not settle below the hand");
        for(int every:{2,3}) {
            const auto [m,s]=run(every);
            check(std::abs(m-most)<.1f&&length(s-settled)<.04f,"the swing depends on the physics rate");
        }
    });
    test("a heavy target lags the hand that a light one follows", [] {
        auto lag=[](float mass) {
            TestWorld world;world.enabled=false;
            GrabTargets targets;targets.add(7,{0,1,-1.3f},mass);
            WebGrab grab;Body player{{0,1,0},{},true};
            auto in=forward();in.hands[0].trigger=0;grab.claim(1.f/90,in,world,targets,player);
            in.hands[0].trigger=1;
            for(int i=0;i<180;++i)settle(grab,targets,world,player,in,1.f/90);
            check(grab.grabs()[0].phase==GrabPhase::Held,"not held");
            in.hands[0].aim.position.x=2;
            for(int i=0;i<18;++i)settle(grab,targets,world,player,in,1.f/90);
            return 2-targets.find(7)->position.x;
        };
        check(lag(150)>lag(25)+.1f,"mass made no difference to carrying");
        check(lag(150)<1.9f,"a heavy target was not carried at all");
    });
    test("tension tells how hard the web pulls", [] {
        TestWorld world;world.enabled=false;
        GrabTargets targets;targets.add(7,{0,1,-1.3f},60);
        WebGrab grab;Body player{{0,1,0},{},true};
        auto in=forward();in.hands[0].trigger=0;grab.claim(1.f/90,in,world,targets,player);
        near(grab.grabs()[0].tension,0); // slack
        in.hands[0].trigger=1;
        for(int i=0;i<450;++i)settle(grab,targets,world,player,in,1.f/90);
        // Hanging: its weight against the web's strength.
        near(grab.grabs()[0].tension,9.81f*60/grab.config().webForce,.03f);
        // The hand snatched away: the web pulls as hard as it can.
        in.hands[0].aim.position={0,4,0};settle(grab,targets,world,player,in,1.f/90);
        near(grab.grabs()[0].tension,1,.01f);
    });
    test("letting go throws with the arm's speed, never multiplying the player's own", [] {
        TestWorld world;world.enabled=false;
        GrabTargets targets;targets.add(7,{0,1,-1.3f});
        WebGrab grab;Body player{{0,1,0},{20,0,0},false};
        auto in=forward();in.hands[0].trigger=0;grab.claim(1.f/90,in,world,targets,player);
        in.hands[0].trigger=1;
        for(int i=0;i<180;++i)settle(grab,targets,world,player,in,1.f/90);
        check(grab.grabs()[0].phase==GrabPhase::Held,"not held");
        targets.items[0].velocity={20,3,0}; // carried along, plus 3 m/s up from the arm
        in.hands[0].grip=0;grab.claim(1.f/90,in,world,targets,player);
        check(events(grab,GrabEventKind::Throw)==1,"no throw");
        std::vector<TargetCommand> out;grab.step(1.f/120,world,targets,out);
        check(out.size()==1&&out[0].thrown,"the throw was not sent");
        near(out[0].velocity.x,20);near(out[0].velocity.y,3*grab.config().throwMultiplier);
        out.clear();grab.step(1.f/120,world,targets,out);
        check(out.empty(),"the web kept acting after the throw");
    });
    test("a throw near a character is aimed into it", [] {
        TestWorld world;world.enabled=false;
        GrabTargets targets;targets.add(7,{0,1,-1.3f});
        targets.add(9,{3,1,-25},80,.4f,TargetKind::Character);
        WebGrab grab;Body player{{0,1,0},{},true};
        auto in=forward();in.hands[0].trigger=0;grab.claim(1.f/90,in,world,targets,player);
        in.hands[0].trigger=1;
        for(int i=0;i<180;++i)settle(grab,targets,world,player,in,1.f/90);
        targets.items[0].velocity={0,1,-12}; // a throw a few degrees off
        in.hands[0].grip=0;grab.claim(1.f/90,in,world,targets,player);
        std::vector<TargetCommand> out;grab.step(1.f/120,world,targets,out);
        check(out.size()==1,"no throw");
        // Fly the launch ballistically: it must pass through the character.
        Vec3 p=targets.items[0].position,v=out[0].velocity;float closest=1e9f;
        for(int i=0;i<2000;++i){v.y-=9.81f/1000;p+=v/1000;closest=std::min(closest,length(p-targets.items[1].position));}
        check(closest<.15f,"the aimed throw missed");
        near(length(out[0].velocity),length(Vec3{0,1,-12})*grab.config().throwMultiplier,.01f);
    });
    test("aimed throws reach a target only within their cone, flatter arc first", [] {
        check(!aimThrow({},{0,0,-10},{10,0,-10},.2f,9.81f),"a target 45 degrees off was aimed at");
        const auto v=aimThrow({},{0,0,-20},{0,0,-30},.2f,9.81f);
        check(v.has_value(),"a target straight ahead was refused");
        near(length(*v),20);
        // 20 m/s reaches 30 m at 23.7 or 66.3 degrees: the flatter one.
        near(v->y,20*std::sin(std::atan((400-std::sqrt(400*400-9.81f*9.81f*900))/(9.81f*30))),.01f);
        const auto direct=aimThrow({},{0,0,-5},{0,0,-30},.2f,9.81f);
        check(direct&&std::abs(direct->y)<1e-4f,"out of reach did not fall back to a straight throw");
        near(rayMiss({},{0,0,-1},50,{0,0,-10},1),0);
        check(std::isinf(rayMiss({},{0,0,-1},50,{0,0,10},1)),"a target behind the hand was in reach");
        check(std::isinf(rayMiss({},{0,0,-1},5,{0,0,-10},1)),"a target beyond reach was in reach");
        near(rayMiss({},{0,0,-1},50,{2,0,-10},1),std::atan2(2.f,10.f)-std::asin(1/std::sqrt(104.f)),1e-4f);
    });
    test("a flight that loses its speed struck something, and hurts by how much it lost", [] {
        const StrikeConfig c;
        // A flight that keeps its speed, or a slow one stopped, struck nothing.
        near(impactLoss(12,11.6f,c),0);
        near(impactLoss(5,0,c),0);
        // Falling faster under gravity is no impact.
        near(impactLoss(10.8f,11.1f,c),0);
        // Yanked at 12 m/s into a wall: it kept 2.
        near(impactLoss(12,2,c),10);
        const auto wall=impactBlow(10,c);
        near(wall.damage,c.minDamage+3*c.damagePerSpeed);
        check(wall.knockback==2&&!wall.fling,"a wall at 10 m/s did more than stagger");
        const auto landing=impactBlow(20,c);
        check(landing.knockback==4,"a 20 m/s landing did not knock down");
        near(impactBlow(60,c).damage,c.maxDamage);
        near(impactBlow(1,c).damage,c.minDamage);
        check(!(impactLoss(12,std::numeric_limits<float>::quiet_NaN(),c)>0),"a torn speed read as an impact");
    });
    test("a flying body strikes a standing character it touches, flinging him when fast", [] {
        const StrikeConfig c;const Vec3 feet{0,0,0};
        check(touches({.6f,1,0},.45f,feet,c),"a prop at his hip missed");
        check(!touches({1,1,0},.45f,feet,c),"a prop a metre off struck");
        check(touches({0,2.1f,0},.45f,feet,c)&&!touches({0,2.4f,0},.45f,feet,c),"his head's height wrong");
        check(!touches({0,-.9f,0},.45f,feet,c),"a body under the floor struck");
        const auto slow=strikeBlow(7,c),fast=strikeBlow(15,c);
        check(!slow.fling&&slow.knockback==4,"a 7 m/s strike flung, or did not knock down");
        check(fast.fling&&fast.knockback==5,"a 15 m/s strike did not fling");
        check(fast.damage>slow.damage&&fast.damage<=c.maxDamage,"strike damage did not grow with speed");
    });
    test("a web pulls a character in when it reels, a hand pulls away fast or yanks, not when it only holds him", [] {
        const PullConfig c;const Vec3 at{0,1,-5};
        TargetCommand rope{7,TargetKind::Character,TargetCommand::Mode::Rope};
        rope.ropeCount=1;rope.ropes[0]={{0,1.3f,0},{},5};
        // At the end of his web, walking off it: it only holds him.
        check(!pullsIn(rope,{0,1,-5.02f},c),"a web that only held him pulled him in");
        // Wound in half a metre shorter than he stands from the hand: a reel.
        rope.ropes[0].length=4.5f;
        check(pullsIn(rope,at,c),"a reel did not pull him in");
        // The hand pulling away from him at 4 m/s; coming toward him.
        rope.ropes[0].length=5;rope.ropes[0].anchorVelocity={0,0,4};
        check(pullsIn(rope,at,c),"a hand pulling away fast did not pull him in");
        rope.ropes[0].anchorVelocity={0,0,-4};
        check(!pullsIn(rope,at,c),"a hand coming toward him pulled him in");
        TargetCommand yank{7,TargetKind::Character,TargetCommand::Mode::Launch};yank.velocity={0,4,10};
        check(pullsIn(yank,at,c),"a yank's jerk did not pull him in");
        yank.thrown=true;
        check(!pullsIn(yank,at,c),"a throw read as a pull");
        const TargetCommand brake{7,TargetKind::Character,TargetCommand::Mode::Follow};
        check(!pullsIn(brake,at,c),"a catch's brake read as a pull");
    });
    test("a pull launches a character onto an arc that reaches the hand", [] {
        const PullConfig c;const Vec3 gravity{0,-9.81f,0},from{0,1,-12};
        TargetCommand rope{7,TargetKind::Character,TargetCommand::Mode::Rope};
        rope.ropeCount=1;rope.ropes[0]={{0,1.3f,0},{},11.5f};
        // Off the ground, toward the hand: a pull from his standing pace kept
        // him on the ground, and the game ended his flight there.
        const Vec3 v=pullLaunch(rope,from,gravity,c);
        check(v.y>2&&v.z>0,"the launch did not lift him toward the hand");
        const float time=length(rope.ropes[0].anchor-from)/c.launchSpeed;
        near(length(from+v*time+gravity*(.5f*time*time)-rope.ropes[0].anchor),0,.02f);
        // Two webs pull toward between both hands.
        rope.ropeCount=2;rope.ropes[1]={{.6f,1.3f,0},{},11.5f};
        check(pullLaunch(rope,from,gravity,c).x>0,"two webs did not pull toward between the hands");
        // Close to the hand the arc takes its shortest time, not a hard throw.
        check(length(pullLaunch(rope,{.3f,1,-1},gravity,c))<6,"a close pull launched hard");
        TargetCommand yank{7,TargetKind::Character,TargetCommand::Mode::Launch};yank.velocity={0,4,10};
        near(pullLaunch(yank,from,gravity,c).z,10);
        const TargetCommand none{7,TargetKind::Character,TargetCommand::Mode::Rope};
        near(length(pullLaunch(none,from,gravity,c)),0);
    });
    test("a grip held after its grab is lost never shoots a swing web", [] {
        TestWorld world;world.anchor={0,20,-30};
        GrabTargets targets;targets.add(7,{0,1,-8});
        WebGrab grab;Swing swing(inert());Body player{{0,1,0},{},true};
        auto in=forward();
        grab.claim(1.f/90,in,world,targets,player);
        targets.items.clear(); // despawned
        std::vector<TargetCommand> out;grab.step(1.f/120,world,targets,out);
        check(grab.grabs()[0].phase==GrabPhase::None&&events(grab,GrabEventKind::Lost)==1,"a vanished target stayed grabbed");
        for(int i=0;i<10;++i) {
            const auto forSwing=grab.claim(1.f/90,in,world,targets,player);
            swing.predictNativeStep(.01f,forSwing,world,player);
            check(!swing.webs()[0].attached,"the held grip shot a swing web");
        }
        in.hands[0].grip=0;swing.predictNativeStep(.01f,grab.claim(1.f/90,in,world,targets,player),world,player);
        in.hands[0].grip=1;swing.predictNativeStep(.01f,grab.claim(1.f/90,in,world,targets,player),world,player);
        check(swing.webs()[0].attached,"a fresh press with nothing to grab did not swing");
    });
    test("a web striking a long prop's near end takes it, and its own body never cuts the web", [] {
        // A bench whose near end, 1.5 m before its centre, is the first thing
        // the hand's ray meets: a static body, as the game's props are.
        struct Bench : TestWorld {
            Vec3 centre{0,1,-8};
            std::optional<RayHit> raycast(Vec3 o,Vec3 d,float distance) const override {
                const float t=dot(centre-o,d)-1.5f;
                if(t>0&&t<=distance&&length(o+d*(t+1.5f)-centre)<.5f)return RayHit{o+d*t,{0,0,1},77,true};
                return {};
            }
        } world;
        GrabTargets targets;targets.add(7,world.centre);
        WebGrab grab;Body player{{0,1,0},{},true};
        auto in=forward();in.hands[0].trigger=0;
        grab.claim(1.f/90,in,world,targets,player);
        check(grab.grabs()[0].phase==GrabPhase::None,"a prop behind the bench was taken without its owner");
        targets.owners.push_back({77,7});
        in.hands[0].grip=0;grab.claim(1.f/90,in,world,targets,player);
        in.hands[0].grip=1;grab.claim(1.f/90,in,world,targets,player);
        check(grab.grabs()[0].phase==GrabPhase::Tethered&&grab.grabs()[0].target==7,"the struck prop was not taken");
        std::vector<TargetCommand> out;
        for(int i=0;i<120;++i)grab.step(1.f/120,world,targets,out);
        check(grab.grabs()[0].phase==GrabPhase::Tethered,"the prop's own body cut its web");
    });
    test("a wall that stays between hand and target cuts the web", [] {
        struct Gate : TestWorld {
            bool closed{};
            std::optional<RayHit> raycast(Vec3 o,Vec3 d,float distance) const override {
                if(!closed||d.z>=0)return {};
                const float t=(o.z+4)/-d.z;
                return t<=distance?std::optional<RayHit>(RayHit{o+d*t,{0,0,1},3,true}):std::nullopt;
            }
        } world;
        GrabTargets targets;targets.add(7,{0,1,-8});
        WebGrab grab;Body player{{0,1,0},{},true};
        auto in=forward();grab.claim(1.f/90,in,world,targets,player);
        world.closed=true;
        std::vector<TargetCommand> out;
        for(int i=0;i<24;++i)grab.step(1.f/120,world,targets,out);
        check(grab.grabs()[0].phase==GrabPhase::Tethered,"a passing obstruction cut the web");
        for(int i=0;i<24;++i)grab.step(1.f/120,world,targets,out);
        check(grab.grabs()[0].phase==GrabPhase::None,"a lasting wall kept the web");
    });
    test("tracking or focus loss drops grabs until the grip is released", [] {
        TestWorld world;world.enabled=false;
        GrabTargets targets;targets.add(7,{0,1,-8});
        WebGrab grab;Body player{{0,1,0},{},true};
        auto in=forward();grab.claim(1.f/90,in,world,targets,player);
        in.hands[0].tracked=false;grab.claim(1.f/90,in,world,targets,player);
        check(grab.grabs()[0].phase==GrabPhase::None,"tracking loss kept the grab");
        in.hands[0].tracked=true;
        check(grab.claim(1.f/90,in,world,targets,player).hands[0].grip==0&&grab.grabs()[0].phase==GrabPhase::None,"a held grip regrabbed");
        in.hands[0].grip=0;grab.claim(1.f/90,in,world,targets,player);
        in.hands[0].grip=1;grab.claim(1.f/90,in,world,targets,player);
        check(grab.grabs()[0].phase==GrabPhase::Tethered,"cannot grab again");
        in.focused=false;grab.claim(1.f/90,in,world,targets,player);
        check(grab.grabs()[0].phase==GrabPhase::None,"focus loss kept the grab");
    });
    test("two hands carry one target together and throw it once", [] {
        TestWorld world;world.enabled=false;
        GrabTargets targets;targets.add(7,{0,1,-1.3f});
        WebGrab grab;Body player{{0,1,0},{},true};
        auto in=forward();in.hands[1]=in.hands[0];in.hands[1].aim.position.x=.2f;
        in.hands[0].trigger=in.hands[1].trigger=0;grab.claim(1.f/90,in,world,targets,player);
        in.hands[0].trigger=in.hands[1].trigger=1;
        for(int i=0;i<180;++i)settle(grab,targets,world,player,in,1.f/90);
        check(grab.grabs()[0].phase==GrabPhase::Held&&grab.grabs()[1].phase==GrabPhase::Held,"not held by both");
        near(targets.find(7)->position.x,.1f,.03f);
        targets.items[0].velocity={0,0,-6};
        in.hands[0].grip=in.hands[1].grip=0;grab.claim(1.f/90,in,world,targets,player);
        check(events(grab,GrabEventKind::Throw)==2,"both hands did not let go");
        std::vector<TargetCommand> out;grab.step(1.f/120,world,targets,out);
        check(out.size()==1,"one target was thrown twice");
    });
    test("lab: a thrown crate knocks a thug over, who gets up again", [] {
        LabWorld world({{{-50,-1,-50},{50,0,50},{},1}});
        std::vector<LabProp> list(2);
        list[0].id=1;list[0].position={0,.32f,0};list[0].radius=.32f;list[0].mass=25;
        list[1].id=2;list[1].kind=TargetKind::Character;list[1].mass=80;list[1].radius=.4f;
        list[1].position={0,LabProps::standingHeight,-6};
        LabProps props(list);
        TargetCommand throwIt{1,TargetKind::Object,TargetCommand::Mode::Launch,true};throwIt.velocity={0,4,-14};
        props.step(1.f/120,std::span(&throwIt,1),world);
        for(int i=0;i<120;++i)props.step(1.f/120,{},world);
        check(props.knockdowns()==1&&props.props()[1].stance==LabProp::Stance::Tumbling,"the hit did not knock the thug over");
        check(props.props()[1].position.z<-6.05f,"the thug took none of the crate's momentum");
        for(int i=0;i<120*6;++i)props.step(1.f/120,{},world);
        check(props.props()[1].stance==LabProp::Stance::Standing,"the thug never got up");
        near(props.props()[1].position.y,LabProps::standingHeight,.02f);
    });
    test("lab: props settle on the ground without sinking or buzzing", [] {
        LabWorld world({{{-50,-1,-50},{50,0,50},{},1}});
        std::vector<LabProp> list(1);
        list[0].id=1;list[0].position={0,3,0};list[0].radius=.32f;list[0].velocity={4,0,1};
        LabProps props(list);
        for(int i=0;i<120*4;++i)props.step(1.f/120,{},world);
        const auto& p=props.props()[0];
        near(p.position.y,.32f,.01f);
        check(length(p.velocity)<.05f,"a prop on the ground kept moving");
        const auto before=p.position;
        for(int i=0;i<120;++i)props.step(1.f/120,{},world);
        check(length(props.props()[0].position-before)<.002f,"a resting prop crept");
    });
    test("lab: the web grab drives lab props end to end", [] {
        LabWorld world({{{-50,-1,-50},{50,0,50},{},1}});
        std::vector<LabProp> list(1);
        list[0].id=1;list[0].position={0,.32f,-10};list[0].radius=.32f;list[0].mass=25;
        LabProps props(list);
        WebGrab grab;Body player{{0,1,0},{},true};
        // Aimed 0.108 rad down from 1.4 m: at the crate's centre 10 m ahead.
        Input in;in.hands[0].aim={{0,1.4f,0},Quat{std::sin(-.054f),0,0,std::cos(-.054f)}};
        in.hands[0].tracked=true;in.hands[0].grip=1;
        grab.claim(1.f/90,in,world,props,player);
        check(grab.grabs()[0].phase==GrabPhase::Tethered,"the aimed crate was not grabbed");
        in.hands[0].trigger=1;
        for(int i=0;i<360;++i){grab.claim(1.f/90,in,world,props,player);props.advance(1.f/90,grab,world);}
        check(grab.grabs()[0].phase==GrabPhase::Held,"reeling never brought the crate in");
        // It hangs from the hand on its short web.
        const auto crate=props.props()[0].position;
        check(length(crate-Vec3{0,1.4f,0})<grab.config().holdDistance+.32f+.1f&&crate.y<1.4f-.3f,"the crate is not hanging at the hand");
        in.hands[0].grip=0;grab.claim(1.f/90,in,world,props,player);props.advance(1.f/90,grab,world);
        for(int i=0;i<300;++i){grab.claim(1.f/90,in,world,props,player);props.advance(1.f/90,grab,world);}
        near(props.props()[0].position.y,.32f,.02f);
    });
    test("grab configuration rejects invalid tuning", [] {
        auto bad=[](auto change){GrabConfig c;change(c);try{WebGrab g(c);return false;}catch(const std::invalid_argument&){return true;}};
        check(bad([](GrabConfig& c){c.webForce=0;}),"a web with no strength accepted");
        check(bad([](GrabConfig& c){c.tetherResponse=-1;}),"a negative web response accepted");
        check(bad([](GrabConfig& c){c.swingDamping=std::numeric_limits<float>::infinity();}),"infinite damping accepted");
        check(bad([](GrabConfig& c){c.minYankFlight=30;}),"inverted yank speeds accepted");
        check(bad([](GrabConfig& c){c.gravity=std::numeric_limits<float>::quiet_NaN();}),"NaN gravity accepted");
        check(!bad([](GrabConfig&){}),"defaults rejected");
    });
    test("body: the rest pose gives the references and broken rigs are refused", [] {
        auto b=testBody();
        near(b.rig.eyeHeight,1.7f);
        near(b.rig.eyesFromHead.x,.08f);near(b.rig.eyesFromHead.y,.08f);near(b.rig.eyesFromHead.z,0);
        near(b.rig.upperArm[0],.26f);near(b.rig.forearm[1],.3f);near(b.rig.thigh[0],.4504f,.001f);
        check(b.rig.below[b.rig.arms[0].upper].size()==14,"the left arm's subtree is wrong");
        auto broken=b;broken.rig.arms[0].fingers={{29,30,99}};
        check(!body::prepare(broken.rig,broken.rest),"a finger beyond the rig was accepted");
        auto cycle=b;cycle.rig.parent[2]=4;
        check(!body::prepare(cycle.rig,cycle.rest),"a cycle in the parents was accepted");
        auto missing=b;missing.rig.arms[1].hand=-1;
        check(!body::prepare(missing.rig,missing.rest),"a rig without a hand was accepted");
        auto lying=b;
        body::Pose view(lying.rest.data(),static_cast<int>(lying.rig.parent.size()));
        view.turn(lying.rig.all,body::axisAngle({1,0,0},1.5f),{});
        check(!body::prepare(lying.rig,lying.rest),"a rest pose lying down was accepted");
    });
    test("body: hands reach the controllers and every bone keeps its length", [] {
        for(const bool odd:{false,true}){
            auto b=testBody(odd);
            auto t=lookingAhead({0,1.7f,.08f});
            t.hands[0]={true,{.3f,1.2f,.45f},gripFacing({0,0,1})};
            t.hands[1]={true,{-.25f,1.55f,.4f},gripFacing({0,.5f,.866f})};
            body::Config c;c.hideHead=false;
            body::State s;body::Result r;
            const auto pose=solved(b,t,s,c,1,&r);
            check(r.solved&&r.weight==1,"the body did not take over");
            for(int i=0;i<2;++i){
                const auto& arm=b.rig.arms[i];
                check(length(b.at(pose,arm.hand)-wristOf(t.hands[i],i))<1e-3f,"a wrist missed its controller");
                // The hand's fingers point past the knuckles: the grip's -y.
                const Vec3 fingers=normalized(b.at(pose,arm.finger)-b.at(pose,arm.hand));
                check(dot(fingers,t.hands[i].orientation.rotate({0,-1,0}))>.999f,"a hand does not follow its controller");
            }
            // Every bone of the body; the rig's root (0) is not the body's and stays.
            for(size_t j=0;j<b.rig.parent.size();++j){
                const int p=b.rig.parent[j];
                if(p<=0)continue;
                near(length(b.at(pose,static_cast<int>(j))-b.at(pose,p)),length(b.at(b.rest,static_cast<int>(j))-b.at(b.rest,p)),1e-4f);
            }
            // Elbows hang below the shoulders.
            check(b.at(pose,b.rig.arms[0].lower).y<b.at(pose,b.rig.arms[0].upper).y,"the left elbow went up");
            // The rig's own root (an effects or camera target in the game's) is not the body's.
            check(std::equal(pose.begin(),pose.begin()+16,b.rest.begin()),"the rig's root moved with the body");
        }
    });
    test("body: the head joint sits behind the eyes and shrinks out of view", [] {
        for(const bool odd:{false,true}){
            auto b=testBody(odd);
            // Looking 30 degrees to the left and 20 down.
            auto t=lookingAhead({.2f,1.62f,.3f});
            t.facing=Quat::yaw(3.14159265f+.52f)*body::axisAngle({1,0,0},-.35f);
            body::Config c;c.hideHead=false;
            body::State s;body::Result r;
            auto pose=solved(b,t,s,c,1,&r);
            check(r.headError<1e-3f,"the head missed its place");
            const Vec3 eyes=(b.at(pose,7)+b.at(pose,8))/2,head=b.at(pose,6);
            check(length(eyes-t.eyes)<1e-3f,"the model's eyes are not at the headset's");
            s={};c.hideHead=true;
            pose=solved(b,t,s,c);
            check(length(b.at(pose,7)-b.at(pose,6))<1e-3f&&length(b.at(pose,8)-b.at(pose,6))<1e-3f,"the head was not shrunk");
            check(length(b.at(pose,6)-head)<1e-4f,"the head joint moved as it shrank");
        }
    });
    test("body: hips stand upright facing the body's way, whatever the game's pose", [] {
        auto b=testBody(true);
        // The game's pose swings face down (a web swing) and turned around.
        auto swinging=b.rest;
        body::Pose view(swinging.data(),static_cast<int>(b.rig.parent.size()));
        view.turn(b.rig.all,body::axisAngle({0,1,0},2.f)*body::axisAngle({1,0,0},1.3f),{0,1,0});
        auto t=lookingAhead({0,1.7f,.08f});
        t.airborne=true;
        body::State s;
        const auto pose=solved(b,t,s,{},1,nullptr,&swinging);
        const Vec3 spine=normalized(b.at(pose,4)-b.at(pose,1));
        check(spine.y>.98f,"the torso is not upright");
        const Vec3 left=normalized(b.at(pose,21)-b.at(pose,25));
        check(left.x>.98f,"the hips do not face the headset's way");
    });
    test("body: standing feet stay on the ground while the player crouches", [] {
        auto b=testBody(true);
        auto t=lookingAhead({0,1.35f,.1f});
        body::State s;body::Result r;
        auto pose=solved(b,t,s,{},1,&r);
        check(r.grounded,"feet on the ground were not seen as standing");
        for(const int foot:{23,27}){
            check(length(b.at(pose,foot)-b.at(b.rest,foot))<1e-3f,"a standing foot moved");
        }
        // Knees bend forward, ahead of the line from hip to ankle.
        for(const auto& leg:b.rig.legs){
            const Vec3 hip=b.at(pose,leg.upper),knee=b.at(pose,leg.lower),ankle=b.at(pose,leg.foot);
            check(knee.z>(hip.z+ankle.z)/2+.05f,"a knee does not bend forward");
        }
        // In the air the legs keep the game's pose and go with the hips.
        t.airborne=true;s={};
        pose=solved(b,t,s,{},1,&r);
        check(!r.grounded&&b.at(pose,23).y<b.at(b.rest,23).y-.2f,"airborne legs stayed on the ground");
    });
    test("body: the body turns with the head only past the dead zone", [] {
        auto b=testBody();
        auto t=lookingAhead({0,1.7f,.08f});
        body::State s;
        solved(b,t,s);
        near(s.yaw,0,.01f);
        // 25 degrees to the left: within the dead zone, the hips stay.
        t.facing=Quat::yaw(3.14159265f+.44f);
        std::vector<float> pose=b.rest;
        body::Pose view(pose.data(),static_cast<int>(b.rig.parent.size()));
        body::solve(view,b.rig,t,{},s,true,.05f);
        check(std::abs(s.yaw)<.05f,"the hips turned within the dead zone");
        // 100 degrees: the hips follow to the dead zone's edge.
        t.facing=Quat::yaw(3.14159265f+1.75f);
        for(int i=0;i<20;++i){pose=b.rest;body::solve(view,b.rig,t,{},s,true,.02f);}
        check(s.yaw>1.75f-.65f&&s.yaw<1.75f,"the hips did not follow the head");
    });
    test("body: the game's pose stays untouched until the body blends in, and comes back", [] {
        auto b=testBody(true);
        auto t=lookingAhead({.1f,1.5f,.3f});
        t.hands[0]={true,{.3f,1.f,.4f},gripFacing({0,0,1})};
        body::State s;
        std::vector<float> pose=b.rest;
        body::Pose view(pose.data(),static_cast<int>(b.rig.parent.size()));
        auto r=body::solve(view,b.rig,t,{},s,false,.1f);
        check(!r.solved&&pose==b.rest,"a body that was not wanted moved the pose");
        r=body::solve(view,b.rig,t,{},s,true,.1f);
        check(r.solved&&r.weight>.3f&&r.weight<.5f,"the body did not blend in gradually");
        check(length(b.at(pose,12)-wristOf(t.hands[0],0))>.05f,"half blended, the hand is already there");
        for(int i=0;i<10;++i){pose=b.rest;body::solve(view,b.rig,t,{},s,true,.1f);}
        for(int i=0;i<10;++i){pose=b.rest;r=body::solve(view,b.rig,t,{},s,false,.1f);}
        check(r.weight==0&&pose==b.rest,"the body never gave the pose back");
    });
    test("body: a controller out of reach stretches the arm toward it", [] {
        auto b=testBody(true);
        auto t=lookingAhead({0,1.7f,.08f});
        t.hands[1]={true,{-1.5f,1.5f,1.5f},gripFacing({0,0,1})};
        body::Config c;c.hideHead=false;
        body::State s;body::Result r;
        const auto pose=solved(b,t,s,c,1,&r);
        const Vec3 shoulder=b.at(pose,16),elbow=b.at(pose,17),wrist=b.at(pose,18);
        check(finite(wrist)&&r.handError[1]>1,"an unreachable hand reported reaching");
        check(dot(normalized(elbow-shoulder),normalized(wrist-elbow))>.99f,"the arm is not stretched");
        check(dot(normalized(wrist-shoulder),normalized(wristOf(t.hands[1],1)-shoulder))>.99f,"the arm does not point at the controller");
    });
    test("body: on the game's own crawl the feet are planted under the hips, whatever its legs do", [] {
        for(const bool odd:{false,true}){
            auto b=testBody(odd);
            // The crawl's legs: each thigh swung up and out to its side, the feet well off the floor - the game's
            // crawl splayed on a wall. With the crawling flag the body's up is the model's own +y, the floor y 0.
            auto from=b.rest;
            body::Pose view(from.data(),static_cast<int>(b.rig.parent.size()));
            for(int i=0;i<2;++i){
                const auto& leg=b.rig.legs[i];
                view.turn(b.rig.below[leg.upper],body::axisAngle({0,0,i==0?1.f:-1.f},1.1f),b.at(from,leg.upper));
            }
            check(b.at(from,b.rig.legs[0].foot).y>b.rig.ankleHeight[0]+.2f,"the crawl's legs were not lifted");
            auto t=lookingAhead({0,1.7f,.08f});
            t.plant=true;
            body::State s;body::Result r;
            const auto pose=solved(b,t,s,{},1,&r,&from);
            check(r.grounded,"planted feet were not standing");
            for(int i=0;i<2;++i){
                const auto& leg=b.rig.legs[i];
                const Vec3 foot=b.at(pose,leg.foot),hip=b.at(pose,leg.upper);
                near(foot.y,b.rig.ankleHeight[i],.03f);
                check(std::hypot(foot.x-hip.x,foot.z-hip.z)<.12f,"a planted foot is not under its hip");
            }
            // Without the plant, the same legs stay the game's, off the floor.
            body::State kept;body::Result was;
            const auto left=solved(b,lookingAhead({0,1.7f,.08f}),kept,{},1,&was,&from);
            check(!was.grounded,"the game's lifted feet were taken for standing");
            check(b.at(left,b.rig.legs[0].foot).y>b.rig.ankleHeight[0]+.15f,"the game's lifted foot came down");
            // In the air nothing is planted, crawl or not.
            auto air=lookingAhead({0,1.7f,.08f});
            air.plant=true;air.airborne=true;
            body::State a;body::Result ar;
            solved(b,air,a,{},1,&ar,&from);
            check(!ar.grounded,"feet were planted in the air");
        }
    });
    test("body: on Spidy's own wall the body stands on the wall with the player, never lying", [] {
        // The player, October 14, of a body solved upright in the world while he stood on the wall: "still body
        // in front of me" - "i dont want it to be lying at all". The game holds the hero upright in its air
        // there, 0.9 m off the wall, its legs in its air pose; the player stands on the wall, his up its normal.
        for(const bool odd:{false,true}){
            auto b=testBody(odd);
            auto from=b.rest;
            body::Pose view(from.data(),static_cast<int>(b.rig.parent.size()));
            for(int i=0;i<2;++i){
                const auto& leg=b.rig.legs[i];
                view.turn(b.rig.below[leg.upper],body::axisAngle({1,0,0},-.9f),b.at(from,leg.upper));
            }
            // The wall: its normal the model's +x, its face 0.9 m behind the model's origin.
            const Vec3 up{1,0,0},floor{-.9f,1,0};
            body::Targets t;t.head=true;t.up=up;t.plant=true;t.floor=floor;
            // His eyes 1.7 m out from it, looking along it, up the wall (the model's +y).
            t.eyes=floor+up*1.7f+Vec3{0,.2f,0};
            t.facing=body::fromAxes({0,0,-1},{1,0,0},{0,-1,0});
            body::State s;body::Result r;
            const auto pose=solved(b,t,s,{},1,&r,&from);
            check(r.solved&&r.grounded,"the body on the wall was not standing on it");
            check(dot(normalized(b.at(pose,4)-b.at(pose,1)),up)>.95f,"the torso does not stand along the wall's normal");
            check(r.headError<1e-3f,"the head missed the eyes on the wall");
            for(int i=0;i<2;++i){
                const auto& leg=b.rig.legs[i];
                const Vec3 foot=b.at(pose,leg.foot),hip=b.at(pose,leg.upper);
                near(dot(foot-floor,up),b.rig.ankleHeight[i],.03f);
                const Vec3 across=(foot-hip)-up*dot(foot-hip,up);
                check(length(across)<.12f,"a foot on the wall is not under its hip");
            }
        }
    });
    test("body: held down under a duct's roof the legs kneel behind the player, not up in front of his eyes", [] {
        // The first mission's duct, October 14 19:57: the head held a metre over its floor and the legs laid out
        // in front of the player's eyes. Planted under a held head they kneel: the knees down on the floor, the
        // shins back along it behind him. Folded under the hips instead (a squat), the knees come up in front.
        for(const bool odd:{false,true}){
            auto b=testBody(odd);
            body::Targets t;t.head=true;t.plant=true;t.floor={};
            t.eyes={0,1,0};t.facing={};
            const Vec3 ahead{0,0,-1};
            body::State s;body::Result r;
            t.kneel=true;
            const auto kneeling=solved(b,t,s,{},1,&r);
            check(r.solved&&r.grounded,"the kneeling body was not on the floor");
            check(r.headError<1e-3f,"the kneeling head missed the eyes");
            for(int i=0;i<2;++i){
                const auto& leg=b.rig.legs[i];
                const Vec3 hip=b.at(kneeling,leg.upper),knee=b.at(kneeling,leg.lower);
                const Vec3 foot=b.at(kneeling,leg.foot);
                check(knee.y<.15f,"a kneeling knee is off the floor");
                check(dot(knee-hip,ahead)<.05f,"a kneeling knee is in front of the hip");
                check(dot(foot-hip,ahead)<-.3f,"a kneeling foot is not behind the player");
                near(length(knee-hip),b.rig.thigh[i],.02f);
            }
            body::State q;
            t.kneel=false;
            const auto squatting=solved(b,t,q,{},1,&r);
            float forward=-1;
            for(int i=0;i<2;++i){
                const auto& leg=b.rig.legs[i];
                forward=std::max(forward,dot(b.at(squatting,leg.lower)-b.at(squatting,leg.upper),ahead));
            }
            check(forward>.2f,"a squat under the same head did not fold the knees up in front");
            // Tall enough to stand, kneeling stands.
            body::State u;
            t.kneel=true;t.eyes={0,1.7f,0};
            const auto tall=solved(b,t,u,{},1,&r);
            for(int i=0;i<2;++i){
                const Vec3 hip=b.at(tall,b.rig.legs[i].upper),foot=b.at(tall,b.rig.legs[i].foot);
                const Vec3 across=(foot-hip)-Vec3{0,1,0}*dot(foot-hip,Vec3{0,1,0});
                check(length(across)<.12f,"a standing kneel did not stand");
            }
        }
    });
    test("body: on a wall the body stands up in the world and plants no feet", [] {
        auto b=testBody(true);
        // The hero crawls up a wall: the world's up is the model's -z.
        body::Targets t;t.head=true;t.up={0,0,-1};
        t.eyes=Vec3{0,.3f,0}+t.up*1.7f;
        // Looking level in the world, along the model's +y.
        t.facing=body::fromAxes({-1,0,0},{0,0,-1},{0,-1,0});
        body::State s;body::Result r;
        const auto pose=solved(b,t,s,{},1,&r);
        check(r.solved&&!r.grounded,"feet were planted on a wall");
        check(dot(normalized(b.at(pose,4)-b.at(pose,1)),t.up)>.98f,"the torso does not stand up in the world");
        check(r.headError<1e-3f,"the head missed its place on a wall");
    });
    test("body: a turn of the whole player turns the body at once", [] {
        auto b=testBody();
        auto t=lookingAhead({0,1.7f,.08f});
        body::State s;
        solved(b,t,s);
        // A 30 degree snap turn: within the dead zone, but the whole player
        // turned, and the adapter turns the body by as much.
        body::turnState(s,.5236f);
        t.facing=Quat::yaw(3.14159265f+.5236f);
        std::vector<float> pose=b.rest;
        body::Pose view(pose.data(),static_cast<int>(b.rig.parent.size()));
        const auto r=body::solve(view,b.rig,t,{},s,true,1.f/90);
        near(s.yaw,.5236f,.01f);
        // The hips face the new way at once.
        const Vec3 left=normalized(b.at(pose,21)-b.at(pose,25));
        check(r.solved&&std::abs(std::atan2(-left.z,left.x)-.5236f)<.02f,"the hips did not turn with the player");
    });
    test("body: a fist curls the fingers into the palm, whatever the game had curled", [] {
        for(const bool odd:{false,true}){
            auto b=testBody(odd);
            auto t=lookingAhead({0,1.7f,.08f});
            t.hands[0]={true,{.3f,1.2f,.45f},gripFacing({0,0,1})};
            body::Config c;c.hideHead=false;
            body::State s;
            const auto open=solved(b,t,s,c);
            const Vec3 wrist=b.at(open,12);
            const float reach=length(b.at(open,33)-wrist);
            t.hands[0].fist=1;s={};
            const auto fist=solved(b,t,s,c);
            const Vec3 palm=normalized(body::Pose(const_cast<float*>(fist.data()),38).direction(12,b.rig.handPalm[0]));
            check(length(b.at(fist,33)-wrist)<reach*.6f,"the finger did not close");
            // The tip ends up on the palm's side of the hand.
            check(dot(b.at(fist,33)-b.at(fist,30),palm)>.02f,"the finger closed the wrong way");
            for(const int j:{30,31,32,33,35,36,37})
                near(length(b.at(fist,j)-b.at(fist,j-1)),length(b.at(b.rest,j)-b.at(b.rest,j-1)),1e-4f);
            // The thumb wraps over the curled finger.
            check(length(b.at(fist,37)-b.at(fist,31))<.03f,"the thumb did not wrap the fingers");
            near(length(b.at(fist,12)-wristOf(t.hands[0],0)),0,1e-3f);
        }
    });
    // Spider-Man's hands held out before him, each controller thumb up.
    const auto heroHandsOut=[](float fist,Quat left=gripFacing({0,0,1}),Quat right=gripFacing({0,0,1})){
        auto t=lookingAhead({0,1.7f,.08f});
        t.hands[0]={true,{.3f,1.2f,.45f},left,fist};
        t.hands[1]={true,{-.3f,1.2f,.45f},right,fist};
        return t;
    };
    test("body: each hand's palm faces its controller's, as its knuckles say", [&] {
        for(const bool odd:{false,true}){
            auto b=testBody(odd,true);
            // Down across the fingers, what a rest pose is taken to hold, is 30 degrees off these palms.
            for(int side=0;side<2;++side)near(dot(heroPalm(b,b.rest,side),{0,-1,0}),std::cos(.5236f),.01f);
            const Quat grips[]={gripFacing({0,0,1}),gripFacing({0,.5f,.866f}),
                                gripFacing({.3f,-.2f,1})*body::axisAngle({0,1,0},1.2f)};
            for(const Quat grip:grips){
                body::Config c;c.hideHead=false;
                body::State s;
                const auto pose=solved(b,heroHandsOut(0,grip,grip),s,c);
                // A left palm faces the grip's +x, a right one its -x.
                check(dot(heroPalm(b,pose,0),grip.rotate({1,0,0}))>std::cos(.01f),"the left palm does not face the controller's");
                check(dot(heroPalm(b,pose,1),grip.rotate({-1,0,0}))>std::cos(.01f),"the right palm does not face the controller's");
            }
        }
    });
    test("body: a hand holding nothing is this body's own open hand, never the game's crawl splayed on a wall", [&] {
        for(const bool odd:{false,true}){
            auto b=testBody(odd,true);
            // The game's hands: as at rest, splayed back as its wall crawl
            // holds them on a wall, and curled.
            const float games[3][3]={{0,0,0},{-.4f,-.3f,-.2f},{1.2f,1.3f,.9f}};
            for(const auto& game:games){
                auto from=b.rest;
                body::Pose view(from.data(),static_cast<int>(b.rig.parent.size()));
                for(int side=0;side<2;++side)
                    for(const auto& f:b.rig.arms[side].fingers){
                        const Vec3 hinge=heroHinge(b,from,f,side);
                        for(size_t k=1;k<=3;++k)
                            view.turn(b.rig.below[f[k]],body::axisAngle(hinge,game[k-1]),b.at(from,f[k]));
                    }
                body::Config c;c.hideHead=false;
                // Nothing pressed: every joint sits at this body's own angle,
                // whatever the game had done with it - its wall crawl's splay
                // (the player, October 14: "i dont want the crawling
                // animation on legs or hands ever") and the fists it closes
                // after a swing alike.
                body::State s;
                const auto posed=solved(b,heroHandsOut(0),s,c,1,nullptr,&from);
                for(int side=0;side<2;++side)
                    for(const auto& fin:b.rig.arms[side].fingers)
                        for(size_t k=1;k<=3;++k)
                            near(bendOf(b,posed,fin,k,heroHinge(b,posed,fin,side)),c.openBend[k-1],.02f);
                // And a fist closes over the open hand: the fist's angles win.
                body::State closed;
                const auto made=solved(b,heroHandsOut(1),closed,c,1,nullptr,&from);
                for(int side=0;side<2;++side)
                    for(const auto& fin:b.rig.arms[side].fingers)
                        for(size_t k=1;k<=3;++k)
                            near(bendOf(b,made,fin,k,heroHinge(b,made,fin,side)),c.fistBend[k-1],.02f);
            }
        }
    });
    test("body: a fist closes every finger joint toward the palm and never back, however the game had bent it", [&] {
        for(const bool odd:{false,true}){
            auto b=testBody(odd,true);
            // The game's hands: as at rest, the fingers bent back (pressed flat on a ledge), and a fist of its own
            // closed tighter than Spidy's.
            const float games[3][3]={{0,0,0},{-.26f,-.26f,-.26f},{1.75f,1.9f,1.2f}};
            for(const auto& game:games){
                auto from=b.rest;
                body::Pose view(from.data(),static_cast<int>(b.rig.parent.size()));
                for(int side=0;side<2;++side)
                    for(const auto& f:b.rig.arms[side].fingers){
                        const Vec3 hinge=heroHinge(b,from,f,side);
                        for(size_t k=1;k<=3;++k)
                            view.turn(b.rig.below[f[k]],body::axisAngle(hinge,game[k-1]),b.at(from,f[k]));
                    }
                for(const float fist:{.25f,.5f,1.f}){
                    body::Config c;c.hideHead=false;
                    body::State s;
                    const auto pose=solved(b,heroHandsOut(fist),s,c,1,nullptr,&from);
                    for(int side=0;side<2;++side){
                        const auto& fingers=b.rig.arms[side].fingers;
                        // Every joint goes `fist` of the way from the open hand's bend to the fist's, about its
                        // hinge, however the game had bent it.
                        for(const auto& f:fingers)
                            for(size_t k=1;k<=3;++k){
                                const float was=c.openBend[k-1];
                                near(bendOf(b,pose,f,k,heroHinge(b,pose,f,side)),was+(c.fistBend[k-1]-was)*fist,.01f);
                            }
                        if(fist<1)continue;
                        // Closed, the fingertips are tucked into the palm, each beside the next as on the knuckles.
                        const auto& arm=b.rig.arms[side];
                        const Vec3 palm=heroPalm(b,pose,side),along=normalized(b.at(pose,arm.finger)-b.at(pose,arm.hand));
                        const Vec3 across=normalized(b.at(pose,fingers.back()[1])-b.at(pose,fingers.front()[1]));
                        for(size_t i=0;i<fingers.size();++i){
                            const Vec3 tip=b.at(pose,fingers[i][4]),knuckle=b.at(pose,fingers[i][1]);
                            check(dot(tip-knuckle,palm)>.01f&&dot(tip-knuckle,along)<-.01f,"a fingertip is not in the fist");
                            if(i)check(dot(tip-b.at(pose,fingers[i-1][4]),across)>.005f,"the fingertips crossed");
                            // Gathered: the tips no further apart than their knuckles, where the rest pose's spread
                            // fanned them 1.8 times as far (October 18).
                            if(i)check(length(tip-b.at(pose,fingers[i-1][4]))<
                                           1.3f*length(knuckle-b.at(pose,fingers[i-1][1])),"the fist's fingers fan out");
                        }
                    }
                }
            }
        }
    });
    test("body: nothing the game's animation does to a hand shows: palm, spread, twist, thumb and wrist alike", [&] {
        for(const bool odd:{false,true}){
            auto b=testBody(odd,true);
            // The game's hand: the wrist bent, each bone in the palm spread
            // and twisted, every finger joint and the thumb turned off its
            // hinge - all of what only bending the joints past the palm left
            // in the hand ("Hand poses for web shooting (and in general) seem
            // to be affected by some animations", October 15).
            auto from=b.rest;
            body::Pose view(from.data(),static_cast<int>(b.rig.parent.size()));
            for(int side=0;side<2;++side){
                const auto& arm=b.rig.arms[side];
                view.turn(b.rig.below[arm.hand],body::axisAngle({.3f,1,-.4f},.7f),b.at(from,arm.hand));
                int n=0;
                for(const auto& f:arm.fingers)
                    for(size_t k=0;k<4;++k,++n)
                        view.turn(b.rig.below[f[k]],body::axisAngle({std::sin(n*1.7f),1,std::cos(n*.9f)},.25f+.05f*k),
                                  b.at(from,f[k]));
                for(size_t k=0;k+1<arm.thumbChain.size();++k)
                    view.turn(b.rig.below[arm.thumbChain[k]],body::axisAngle({1,.2f*k,-.5f},.5f),
                              b.at(from,arm.thumbChain[k]));
            }
            for(const auto [fist,shoot]:{std::pair{0.f,0.f},std::pair{1.f,0.f},std::pair{.5f,0.f},std::pair{0.f,1.f}}){
                auto t=heroHandsOut(fist);
                t.hands[0].shoot=t.hands[1].shoot=shoot;
                body::Config c;c.hideHead=false;
                body::State fromRest,fromGame;
                const auto clean=solved(b,t,fromRest,c);
                const auto posed=solved(b,t,fromGame,c,1,nullptr,&from);
                // The same hand to the joint, its frame and all, as from the rest pose.
                for(int side=0;side<2;++side)
                    for(const auto j:b.rig.below[b.rig.arms[side].hand])
                        for(int i=0;i<15;++i)
                            near(posed[j*16+i],clean[j*16+i],5e-4f);
            }
        }
    });
    test("body: a hand the headset lost stays where it was last tracked from the body, not the game's arm", [&] {
        for(const bool odd:{false,true}){
            auto b=testBody(odd,true);
            body::Config c;c.hideHead=false;
            body::State s;
            const auto t=heroHandsOut(0);
            const auto first=solved(b,t,s,c);
            // Lost, the controller's report says nothing; the player steps
            // half a metre to the side, his eyes with him.
            auto lost=t;
            lost.hands[0].tracked=false;
            lost.hands[0].grip={};
            lost.eyes+=Vec3{.5f,0,0};
            auto moved=t.hands[1];
            moved.grip+=Vec3{.5f,0,0};
            lost.hands[1]=moved;
            const auto pose=solved(b,lost,s,c);
            const auto& arms=b.rig.arms;
            near(length(b.at(pose,arms[0].hand)-(wristOf(t.hands[0],0)+Vec3{.5f,0,0})),0,1e-3f);
            near(length(b.at(pose,arms[1].hand)-wristOf(moved,1)),0,1e-3f);
            // Its hand turned as it was, open.
            near(dot(heroPalm(b,pose,0),heroPalm(b,first,0)),1,1e-3f);
            for(const auto& fin:arms[0].fingers)
                for(size_t k=1;k<=3;++k)
                    near(bendOf(b,pose,fin,k,heroHinge(b,pose,fin,0)),c.openBend[k-1],.02f);
        }
    });
    test("body: a fist's thumb bends in one plane and lies on the curled fingers", [&] {
        for(const bool odd:{false,true}){
            auto b=testBody(odd,true);
            body::Config c;c.hideHead=false;
            body::State s;
            const auto pose=solved(b,heroHandsOut(1),s,c);
            for(int side=0;side<2;++side){
                const auto& f=b.rig.arms[side].fingers;
                const auto* thumb=heroThumbs[side];
                // Its tip on the middle bones of the index and middle fingers, out of the fist.
                const Vec3 onto=(b.at(pose,f[0][2])+b.at(pose,f[0][3])+b.at(pose,f[1][2])+b.at(pose,f[1][3]))/4+
                                heroPalm(b,pose,side)*c.thumbRest;
                check(length(b.at(pose,thumb[3])-onto)<.003f,"the thumb's tip is not on the fingers");
                // Both joints bend the same way about one hinge: no twist, no kink.
                const Vec3 base=b.at(pose,thumb[1])-b.at(pose,thumb[0]),middle=b.at(pose,thumb[2])-b.at(pose,thumb[1]),
                           last=b.at(pose,thumb[3])-b.at(pose,thumb[2]);
                check(dot(normalized(cross(base,middle)),normalized(cross(middle,last)))>std::cos(.1f),"the thumb twists");
                near(std::atan2(length(cross(middle,last)),dot(middle,last)),c.thumbBend,.03f);
                check(std::atan2(length(cross(base,middle)),dot(base,middle))<c.thumbBendMax+.03f,"the thumb bent too far");
            }
        }
    });
    test("body: the web-shooting hand presses its two middle fingers into the palm and keeps the rest out", [&] {
        for(const bool odd:{false,true}){
            auto b=testBody(odd,true);
            // From the game's open hand and over a closed fist alike; and half of the way from the fist.
            for(const auto [fist,shoot]:{std::pair{0.f,1.f},std::pair{1.f,1.f},std::pair{1.f,.5f}}){
                auto t=heroHandsOut(fist);
                t.hands[0].shoot=t.hands[1].shoot=shoot;
                body::Config c;c.hideHead=false;
                body::State s;
                const auto pose=solved(b,t,s,c);
                for(int side=0;side<2;++side){
                    const auto& arm=b.rig.arms[side];
                    const auto& fingers=arm.fingers;
                    for(size_t f=0;f<fingers.size();++f){
                        const float* to=f==0||f==3?c.shootOut:c.shootPress;
                        for(size_t k=1;k<=3;++k)
                            near(bendOf(b,pose,fingers[f],k,heroHinge(b,pose,fingers[f],side)),
                                 c.fistBend[k-1]+(to[k-1]-c.fistBend[k-1])*shoot,.01f);
                    }
                    near(length(b.at(pose,arm.hand)-wristOf(t.hands[side],side)),0,1e-3f);
                    if(shoot<1)continue;
                    const Vec3 palm=heroPalm(b,pose,side),along=normalized(b.at(pose,arm.finger)-b.at(pose,arm.hand));
                    // The middle and ring fingers' tips in the palm; the index and little fingers out past their knuckles.
                    for(const size_t f:{size_t{1},size_t{2}})
                        check(dot(b.at(pose,fingers[f][4])-b.at(pose,fingers[f][1]),palm)>.01f&&
                              dot(b.at(pose,fingers[f][4])-b.at(pose,fingers[f][1]),along)<0,"a middle finger is not in the palm");
                    // On the web shooter's button: as deep in the palm as a fist's tips, and side by side.
                    {
                        body::State fs;
                        const auto made=solved(b,heroHandsOut(1),fs,c);
                        for(const size_t f:{size_t{1},size_t{2}}){
                            const float press=dot(b.at(pose,fingers[f][4])-b.at(pose,fingers[f][1]),palm);
                            const float fistDepth=dot(b.at(made,fingers[f][4])-b.at(made,fingers[f][1]),heroPalm(b,made,side));
                            check(press>.8f*fistDepth,"a pressing finger stops short of the palm");
                        }
                        check(length(b.at(pose,fingers[2][4])-b.at(pose,fingers[1][4]))<
                                  1.3f*length(b.at(pose,fingers[2][1])-b.at(pose,fingers[1][1])),"the pressing fingers fan out");
                    }
                    for(const size_t f:{size_t{0},size_t{3}})
                        check(dot(normalized(b.at(pose,fingers[f][4])-b.at(pose,fingers[f][1])),along)>.8f,"an outer finger is not out");
                    // The thumb straight, out to its own side in the palm's plane.
                    const auto* thumb=heroThumbs[side];
                    Vec3 aside=b.at(pose,fingers.front()[1])-b.at(pose,fingers.back()[1]);
                    aside=aside-along*dot(aside,along);
                    aside=normalized(aside-palm*dot(aside,palm));
                    const Vec3 way=along*std::cos(c.shootThumb)+aside*std::sin(c.shootThumb);
                    check(dot(normalized(b.at(pose,thumb[3])-b.at(pose,thumb[0])),way)>std::cos(.03f),"the thumb does not point out to its side");
                    const Vec3 base=b.at(pose,thumb[1])-b.at(pose,thumb[0]),middle=b.at(pose,thumb[2])-b.at(pose,thumb[1]),
                               last=b.at(pose,thumb[3])-b.at(pose,thumb[2]);
                    check(dot(normalized(base),normalized(middle))>std::cos(.35f)&&dot(normalized(middle),normalized(last))>std::cos(.35f),
                          "the thumb is not straight");
                    for(const int j:{thumb[1],thumb[2],thumb[3]})
                        near(length(b.at(pose,j)-b.at(pose,j-1)),length(b.at(b.rest,j)-b.at(b.rest,j-1)),1e-4f);
                }
            }
        }
    });
    test("body: a hand of fewer than three fingers presses them all to shoot, its thumb out", [] {
        for(const bool odd:{false,true}){
            auto b=testBody(odd);
            auto t=lookingAhead({0,1.7f,.08f});
            t.hands[0]={true,{.3f,1.2f,.45f},gripFacing({0,0,1}),1,1};
            body::Config c;c.hideHead=false;
            body::State s;
            const auto pose=solved(b,t,s,c);
            const Vec3 palm=normalized(body::Pose(const_cast<float*>(pose.data()),38).direction(12,b.rig.handPalm[0]));
            check(dot(b.at(pose,33)-b.at(pose,30),palm)>.02f,"the finger did not press into the palm");
            check(length(b.at(pose,37)-b.at(pose,31))>.06f,"the thumb stayed on the finger");
            near(length(b.at(pose,12)-wristOf(t.hands[0],0)),0,1e-3f);
        }
    });
    test("body: a taller player gets a body scaled about the feet", [] {
        auto b=testBody();
        auto t=lookingAhead({0,1.87f,.09f});
        t.hands[0]={true,{.3f,1.25f,.45f},gripFacing({0,0,1})};
        body::Config c;c.hideHead=false;
        body::State s;body::Result r;
        const auto pose=solved(b,t,s,c,1.1f,&r);
        near(length(b.at(pose,11)-b.at(pose,10)),.26f*1.1f,1e-4f);
        check(r.headError<1e-3f&&r.handError[0]<1e-3f,"the scaled body missed its targets");
        check(r.grounded&&std::abs(b.at(pose,23).y-b.at(b.rest,23).y*1.1f)<1e-3f,"the scaled feet left the ground");
    });
    test("body: a player's longer arms reach their controllers, each arm scaled about its shoulder", [] {
        auto b=testBody();
        body::Config c;c.hideHead=false;
        // Where the shoulders end up under these eyes, without controllers.
        auto t=lookingAhead({0,1.7f,.08f});
        body::State s;
        const auto free=solved(b,t,s,c);
        // Both wrists straight out to the sides 0.65 m from the shoulders: past the rig's 0.56.
        for(int i=0;i<2;++i){
            const Vec3 side{i==0?1.f:-1.f,0,0};
            const Vec3 wrist=b.at(free,b.rig.arms[i].upper)+side*.65f;
            // Knuckles (the grip's -y) out along the arm, thumb up (-z).
            const Vec3 y=side*-1.f,z{0,-1,0};
            const Quat grip=body::fromAxes(cross(y,z),y,z);
            t.hands[i]={true,wrist-grip.rotate({i==0?-c.wristFromGrip.x:c.wristFromGrip.x,c.wristFromGrip.y,0}),grip};
        }
        body::Result r;
        s={};
        solved(b,t,s,c,1,&r);
        check(r.handError[0]>.05f&&r.handError[1]>.05f,"the rig's own arms reached 0.65 m");
        s={};
        const float longer=.65f/.56f;
        const auto pose=solved(b,t,s,c,1,&r,nullptr,longer);
        check(r.handError[0]<1e-3f&&r.handError[1]<1e-3f&&r.headError<1e-3f,"the longer arms missed their controllers");
        for(const auto& arm:b.rig.arms){
            near(length(b.at(pose,arm.lower)-b.at(pose,arm.upper)),.26f*longer,1e-3f);
            near(length(b.at(pose,arm.hand)-b.at(pose,arm.lower)),.30f*longer,1e-3f);
        }
        near(length(b.at(pose,13)-b.at(pose,12)),.09f*longer,1e-3f); // the hand grows with its arm
        check(length(b.at(pose,1)-b.at(free,1))<1e-3f,"the arms moved the hips");
    });
    test("calibration: the rig's proportions, and the body's and arms' scales within their ranges", [] {
        auto b=testBody();
        const auto p=body_calibration::proportions(b.rig);
        near(p.eyeHeight,1.7f);
        // The test rig's shoulders: 0.08 m behind the eyes, 0.24 below, 0.18 out; arms of 0.56 m.
        near(p.shoulders[0].x,-.08f);near(p.shoulders[0].y,-.24f);near(p.shoulders[0].z,.18f);
        near(p.shoulders[1].z,-.18f);near(p.arms[0],.56f);near(p.arms[1],.56f);
        const body::Rig unready;
        near(body_calibration::proportions(unready).eyeHeight,body_calibration::Proportions{}.eyeHeight);
        near(body_calibration::bodyScale(1.87f,p),1.1f);
        near(body_calibration::bodyScale(.9f,p),body_calibration::minBodyScale);
        near(body_calibration::bodyScale(2.6f,p),body_calibration::maxBodyScale);
        near(body_calibration::bodyScale(std::numeric_limits<float>::quiet_NaN(),p),1);
        near(body_calibration::armScale(0,1.1f,p),1);
        near(body_calibration::armScale(.6f,1,p),.6f/.56f);
        near(body_calibration::armScale(.6f,1.1f,p),.6f/(.56f*1.1f));
        near(body_calibration::armScale(.2f,1,p),body_calibration::minArmScale);
        near(body_calibration::armScale(1.2f,1,p),body_calibration::maxArmScale);
        // Spider-Man's own (the defaults): his eyes 1.697 m high, his shoulder joints 0.27 m below them and his
        // arms 0.56 m from them to the wrists.
        const body_calibration::Proportions hero;
        near(hero.eyeHeight,1.697f,1e-3f);near(hero.shoulders[0].y,-.266f,1e-3f);near(hero.arms[0],.559f,1e-3f);
    });
    test("calibration: a T-pose held a second and a half measures the eyes and the arms", [] {
        body_calibration::Calibration c;
        check(!c.update(tPose(1.62f,.6f))&&c.phase()==body_calibration::Phase::idle,"it ran before it started");
        c.start();
        check(c.phase()==body_calibration::Phase::waiting,"the instructions did not show");
        check(!hold(c,tPose(1.62f,.6f),134),"it finished early");
        check(c.phase()==body_calibration::Phase::holding&&c.hint()==body_calibration::Hint::none&&
                  c.armsReady()[0]&&c.armsReady()[1],"the pose did not count");
        near(c.progress(),134.f/135,1e-3f);
        check(c.update(tPose(1.62f,.6f)),"it did not finish at a second and a half");
        check(c.phase()==body_calibration::Phase::done&&c.progress()==1,"not shown as done");
        const auto& r=c.result();
        near(r.eyeHeight,1.62f);near(r.armLength,.6f);near(r.reach[0],.6f);near(r.reach[1],.6f);
        check(!c.update(tPose(1.8f,.7f))&&c.result().eyeHeight==r.eyeHeight,"a finished calibration measured again");
        c.stop();
        check(c.phase()==body_calibration::Phase::idle&&c.result().armLength>.59f,"stopping lost the result");
        // The longer arm counts: the other is a little bent.
        c.start();
        check(hold(c,tPose(1.75f,.62f,.58f),136),"a slightly bent arm kept it from finishing");
        near(c.result().armLength,.62f);near(c.result().eyeHeight,1.75f);
    });
    test("calibration: the first thing wrong with the pose is what the panel asks for", [] {
        using body_calibration::Hint;
        const auto hintFor=[](body_calibration::Sample s){
            body_calibration::Calibration c;c.start();
            c.update(s);
            return c.hint();
        };
        auto s=tPose(1.62f,.6f);
        s.handTracked[1]=false;
        check(hintFor(s)==Hint::tracking,"an untracked controller");
        check(hintFor(tPose(.9f,.6f))==Hint::standUp,"a seated player");
        // Arms hanging down: the wrists 0.6 m under the shoulders.
        s=tPose(1.62f,.6f);
        for(auto& grip:s.grips)grip.position+=Vec3{grip.position.x<0?.6f:-.6f,-.6f,0};
        check(hintFor(s)==Hint::armsOut,"arms down");
        // Crossed arms: the left wrist on the right.
        s=tPose(1.62f,.6f);
        std::swap(s.grips[0].position,s.grips[1].position);
        check(hintFor(s)==Hint::armsOut,"crossed arms");
        check(hintFor(tPose(1.62f,.3f))==Hint::straight,"bent arms");
        check(hintFor(tPose(1.62f,.6f,.47f))==Hint::straight,"one arm much shorter");
        s=tPose(1.62f,.6f);
        s.head.orientation=body::axisAngle({1,0,0},-.7f);
        check(hintFor(s)==Hint::lookAhead,"looking down");
        s.head.orientation=Quat::yaw(.87f);
        check(hintFor(s)==Hint::lookAhead,"looking to the side");
        check(hintFor(tPose(1.62f,.6f,0,.3f))==Hint::triggers,"triggers let go");
        // Moving: a hand 1 cm a frame (0.9 m/s).
        body_calibration::Calibration c;c.start();
        s=tPose(1.62f,.6f);
        c.update(s);
        s.grips[1].position+=Vec3{0,.01f,0};
        c.update(s);
        check(c.hint()==Hint::still,"a moving hand held still");
        for(int h=0;h<=static_cast<int>(Hint::still);++h)
            check(body_calibration::hintText(static_cast<Hint>(h))[0]!=0,"a hint without text");
    });
    test("calibration: a short lapse pauses the hold, a longer one starts it over", [] {
        body_calibration::Calibration c;c.start();
        hold(c,tPose(1.62f,.6f),45);
        near(c.progress(),1.f/3,1e-3f);
        // 0.2 s with the triggers let go: paused.
        hold(c,tPose(1.62f,.6f,0,0),18);
        check(c.phase()==body_calibration::Phase::holding,"a short lapse ended the hold");
        near(c.progress(),1.f/3,1e-3f);
        // Back in the pose, it goes on; once the triggers count, a lighter squeeze (down to 0.35) keeps counting.
        hold(c,tPose(1.62f,.6f),1);
        check(!hold(c,tPose(1.62f,.6f,0,.4f),80),"it finished early");
        check(c.hint()==body_calibration::Hint::none,"a lighter squeeze stopped counting");
        check(hold(c,tPose(1.62f,.6f),10),"the hold did not go on after the lapse");
        // That lighter squeeze does not start the hold: a trigger counts from 0.6 on.
        c.start();
        hold(c,tPose(1.62f,.6f,0,.5f),10);
        check(c.hint()==body_calibration::Hint::triggers&&c.progress()==0,"a light squeeze started the hold");
        // A lapse past a quarter of a second starts over.
        c.start();
        hold(c,tPose(1.62f,.6f),45);
        hold(c,tPose(1.62f,.3f),27);
        check(c.phase()==body_calibration::Phase::waiting&&c.progress()==0,"a long lapse kept the hold");
    });
    test("calibration panel: ahead of the head, facing it; drawn while the calibration shows", [] {
        const auto panel=body_calibration::panelPose({{.2f,1.6f,.1f},Quat::yaw(1.5707963f)});
        // Looking along -x: the panel 1.4 m that way, a little below the eyes, its front toward the head.
        near(panel.position.x,-1.2f);near(panel.position.y,1.48f);near(panel.position.z,.1f);
        const Vec3 front=panel.orientation.rotate({0,0,1});
        near(front.x,1);near(front.y,0);near(front.z,0);
        body_calibration::View view;
        std::vector<Vertex> out;
        body_calibration::appendView(out,view,{0,1.6f,0});
        check(out.empty(),"an idle calibration drew something");
        view.phase=body_calibration::Phase::waiting;
        view.hint=body_calibration::Hint::armsOut;
        view.panel=body_calibration::panelPose({{0,1.6f,0},{}});
        view.handTracked={true,true};
        view.grips[0].position={-.7f,1.4f,-.1f};
        view.grips[1].position={.7f,1.4f,-.1f};
        body_calibration::appendView(out,view,{0,1.6f,0});
        check(out.size()>1000&&out.size()%3==0,"the panel's text and figure are missing");
        float farthest=0;
        for(const auto& v:out){
            check(finite(v.position)&&finite(v.color),"a vertex not finite");
            farthest=std::min(farthest,v.position.z);
        }
        check(farthest>-1.401f,"something drawn behind the panel");
        // Done: the measurements show instead.
        const size_t waiting=out.size();
        out.clear();
        view.phase=body_calibration::Phase::done;
        view.result={1.63f,.59f,{.59f,.58f}};
        body_calibration::appendView(out,view,{0,1.6f,0});
        check(!out.empty()&&out.size()!=waiting,"the result did not show");
    });
    test("overlay text: capitals, digits and punctuation in strokes on the text's plane", [] {
        for(const char* text:{"BODY CALIBRATION","STAND TALL, LOOK AHEAD","ARMS STRAIGHT OUT TO THE SIDES",
                              "PRESS B TO SKIP","EYE HEIGHT 1.63 M","SPIDER-MAN NOW HAS YOUR SIZE",
                              "REDO IT IN SETTINGS, SPIDY VR","0123456789%:()!?+=/<>'"})
            for(const char* c=text;*c;++c)check(*c==' '||drawable(*c),"a character the panel uses has no glyph");
        for(int h=0;h<=static_cast<int>(body_calibration::Hint::still);++h)
            for(const char* c=body_calibration::hintText(static_cast<body_calibration::Hint>(h));*c;++c)
                check(*c==' '||drawable(*c),"a hint uses a character without a glyph");
        check(drawable('a')&&!drawable('#')&&!drawable('~'),"lower case or unknown characters");
        near(textWidth("calibrate",.04f),textWidth("CALIBRATE",.04f));
        // A glyph 4 units wide in a cell 6 tall; a space 2; 1.5 between glyphs.
        near(textWidth("O",.06f),.04f);
        near(textWidth("OO",.06f),.04f*2+.015f);
        near(textWidth("O O",.06f),.04f*2+.015f*2+.02f);
        near(textWidth("O#O",.06f),textWidth("O O",.06f));
        near(textWidth("",.06f),0);
        near(fittedHeight("OO",.06f,1),.06f);
        near(textWidth("OOOO",fittedHeight("OOOO",.06f,.1f)),.1f);
        std::vector<Vertex> out;
        TextStyle style;style.height=.06f;style.color={1,0,0};
        appendText(out,"H I",{1,2,3},{1,0,0},{0,1,0},style);
        // H: three bars; I: three; two triangles each.
        check(out.size()==6*6,"not one bar per stroke");
        const float half=style.height*style.weight/2,wide=textWidth("H I",.06f);
        for(const auto& v:out){
            check(v.color.x==1&&v.color.y==0,"the text's colour");
            near(v.position.z,3);
            check(v.position.x>=1-half-1e-5f&&v.position.x<=1+wide+half+1e-5f&&v.position.y>=2-half-1e-5f&&
                      v.position.y<=2.06f+half+1e-5f,"a stroke outside the text's box");
        }
        out.clear();
        appendText(out,"X",{0,0,0},{1,0,0},{1,0,0},style);
        check(out.empty(),"text along a degenerate plane");
    });
    test("round lettering: a dot at each of a stroke's points, a stroke between them, and edges that fade", [] {
        TextStyle style;style.height=.06f;style.round=true;style.soft=.002f;
        std::vector<Vertex> out;
        appendText(out,"L",{0,0,0},{1,0,0},{0,1,0},style);
        // L: three points and two strokes. A dot: twelve sides, a triangle and a fading quad each; a stroke:
        // three quads across.
        check(out.size()==3*12*9+2*18,"not a dot at each point and a stroke between");
        const float half=style.height*style.weight/2,reach=half+style.soft+1e-5f;
        bool whole{},faded{};
        for(const auto& v:out){
            whole|=v.alpha==1;faded|=v.alpha==0;
            check(v.position.x>=-reach&&v.position.x<=.04f+reach&&v.position.y>=-reach&&v.position.y<=.06f+reach&&v.position.z==0,
                  "a round stroke outside the text's box");
            // Its corner is round: nothing reaches the box's own.
            check(length(v.position-Vec3{-half-style.soft,-half-style.soft,0})>half*.25f,"a square corner");
        }
        check(whole&&faded,"no soft edge");
        // Without the fade: the dots and strokes alone.
        style.soft=0;out.clear();
        appendText(out,"L",{0,0,0},{1,0,0},{0,1,0},style);
        check(out.size()==3*12*3+2*6,"a hard round stroke has an edge to fade");
        for(const auto& v:out)check(v.alpha==1,"a hard round stroke fades");
    });
    // A right fist driven along `path` at 90 Hz; `relative` is the hand
    // relative to the player (the world position minus the player's travel).
    struct Swing {
        Punches punches;
        std::vector<PunchEvent> out;
        std::vector<PunchTarget> targets{{7,{0,0,-1},1.8f,.3f}};
        void at(Vec3 fist,Vec3 travel={},bool busy=false){
            std::array<PunchHand,2> hands{};
            hands[1]={true,fist,fist-travel,busy};
            punches.update(1.f/90,hands,targets,out);
        }
        // From `a` to `b` at `speed` m/s, the player travelling at `carried`.
        void move(Vec3 a,Vec3 b,float speed,Vec3 carried={},bool busy=false){
            const int n=std::max(1,static_cast<int>(std::lround(length(b-a)/speed*90)));
            for(int i=0;i<=n;++i){const Vec3 travel=carried*(i/90.f);at(a+(b-a)*(static_cast<float>(i)/n)+travel,travel,busy);}
        }
    };
    test("punch: a fist driven into a thug lands one punch, as hard as it went in", [] {
        Swing s;
        s.move({0,1.4f,-.2f},{0,1.4f,-1.f},6);
        check(s.out.size()==1,"one swing did not land exactly one punch");
        const auto& e=s.out[0];
        check(e.target==7&&e.hand==1,"the punch hit the wrong thing");
        near(e.speed,6,.4f);
        check(e.direction.z<-.99f,"the blow points the wrong way");
        near(e.point.z,-.7f,.07f);
        check(e.knockback==Knockback::Knockdown&&e.damage>17&&e.damage<25,"a firm punch did the wrong harm");
        // Pulled back slowly and thrown again: a second punch.
        s.move({0,1.4f,-.8f},{0,1.4f,-.2f},.8f);
        s.move({0,1.4f,-.2f},{0,1.4f,-1.f},4);
        check(s.out.size()==2&&s.out[1].knockback==Knockback::Stagger,"a second, lighter punch was wrong");
    });
    test("punch: touches, grazes, a busy hand and a fist carried along are no punches", [] {
        Swing slow;slow.move({0,1.4f,-.2f},{0,1.4f,-1.f},1);
        check(slow.out.empty(),"a slow touch punched");
        Swing graze;graze.move({-.6f,1.4f,-.63f},{.6f,1.4f,-.63f},7);
        check(graze.out.empty(),"a fast graze across his front punched");
        Swing busy;busy.move({0,1.4f,-.2f},{0,1.4f,-1.f},6,{},true);
        check(busy.out.empty(),"a hand holding a web punched");
        // The player flies through the thug at 30 m/s, the arm still.
        Swing carried;carried.targets[0].feet={0,0,-6};
        carried.move({0,1.4f,0},{0,1.4f,0},1,{0,0,-30});
        for(int i=0;i<30;++i)carried.at({0,1.4f,-30.f*(i+2)/90},{0,0,-30.f*(i+2)/90});
        check(carried.out.empty(),"a fist carried along by the player's flight punched");
    });
    test("punch: an uppercut pops him up and a very hard blow sends him flying", [] {
        Swing upper;upper.move({0,.6f,-.55f},{0,1.9f,-.75f},7);
        check(upper.out.size()==1&&upper.out[0].knockback==Knockback::PopUp,"an uppercut did not pop him up");
        Swing hard;hard.move({0,1.4f,.2f},{0,1.4f,-1.f},12);
        check(hard.out.size()==1&&hard.out[0].knockback==Knockback::SuperFlyBack&&hard.out[0].strength==1,"a very hard punch was weak");
    });
    test("punch: a short punch from within his reach still lands", [] {
        Swing s;
        // The fist starts 0.35 m from his axis, inside the capsule's reach.
        s.move({0,1.4f,-.65f},{0,1.4f,-.85f},5);
        check(s.out.size()==1,"a close-range punch did not land");
    });
    test("punch: a capsule is met on its side and on its caps, and missed beside it", [] {
        const PunchTarget t{1,{0,0,0},1.8f,.3f};
        float share{};Vec3 point{};
        check(sweepCapsule({1,1,0},{0,1,0},.1f,t,share,point),"the side was missed");
        near(share,.6f);near(point.x,.3f);
        check(sweepCapsule({0,3,0},{0,1,0},.1f,t,share,point),"the top was missed");
        near(share,.55f);near(point.y,1.8f);
        check(!sweepCapsule({1,1,0},{1,1,1},.1f,t,share,point),"a fist passing beside hit");
        check(!sweepCapsule({0,2.3f,0},{1,2.3f,0},.1f,t,share,point),"a fist passing overhead hit");
    });
    test("punch: the player turning is no motion of the arm; the arm's own punch through it is", [] {
        // A fist held out at arm's length while the right stick turns the player 240 degrees a
        // second: it sweeps through the world at 2.5 m/s, faster than a punch.
        const Vec3 head{0, 1.6f, 0}, arm{0, -.2f, -.6f};
        const float step = 4.1887902f / 90;
        Punches turned, unturned;
        std::vector<PunchEvent> out;
        const std::vector<PunchTarget> none;
        const auto sample = [&](int i, Vec3 reach) {
            const Quat toWorld = Quat::yaw(-step * static_cast<float>(i));
            std::array<PunchHand, 2> hands{};
            hands[1] = {true, head + toWorld.rotate(reach), toWorld.rotate(reach), false};
            return hands;
        };
        for (int i = 0; i < 30; ++i) {
            if (i)
                turned.turn(-step);
            turned.update(1.f / 90, sample(i, arm), none, out);
            unturned.update(1.f / 90, sample(i, arm), none, out);
        }
        check(unturned.speed(1) > unturned.config().minSpeed, "the test's turn is too slow to look like a punch");
        check(turned.speed(1) < .01f, "the turn counted as the arm's motion");
        // Still turning, the arm punches straight ahead at 5 m/s.
        for (int i = 30; i < 36; ++i) {
            turned.turn(-step);
            turned.update(1.f / 90, sample(i, arm + Vec3{0, 0, -5.f / 90 * static_cast<float>(i - 29)}), none, out);
        }
        near(turned.speed(1), 5, .05f);
    });
    test("punch configuration rejects invalid tuning", [] {
        auto bad=[](auto change){PunchConfig c;change(c);try{Punches p(c);return false;}catch(const std::invalid_argument&){return true;}};
        check(bad([](PunchConfig& c){c.fullSpeed=1;}),"full strength below the punch speed accepted");
        check(bad([](PunchConfig& c){c.maxDamage=1;}),"less damage at full strength accepted");
        check(bad([](PunchConfig& c){c.minSpeed=std::numeric_limits<float>::quiet_NaN();}),"NaN speed accepted");
        check(!bad([](PunchConfig&){}),"defaults rejected");
    });
    test("punch: a swing spent some other way lands nothing until the fist has slowed", [] {
        Swing s;
        // Thrown from further back at 6 m/s, and spent on the way, before it reaches him.
        for(int i=0;i<=24;++i){
            s.at({0,1.4f,.6f-6.f*i/90});
            if(i==6)s.punches.spend(1);
        }
        check(s.out.empty(),"a spent swing punched");
        near(s.punches.velocity(1).z,-6,.5f);
        s.move({0,1.4f,-.8f},{0,1.4f,-.2f},.8f);
        s.move({0,1.4f,-.2f},{0,1.4f,-1.f},6);
        check(s.out.size()==1,"a fist that slowed after a spent swing did not punch");
    });
    // Fighting across the room (web_strike.hpp). The player stands at `feet`,
    // head 1.65 m up; the right fist is thrown from the shoulder.
    struct Open : WorldQueries {
        std::optional<RayHit> raycast(Vec3,Vec3,float) const override { return {}; }
        bool exists(std::uint64_t) const override { return true; }
    };
    // A fixed wall across the room at z = -3.
    struct Walled : WorldQueries {
        std::optional<RayHit> raycast(Vec3 o,Vec3 d,float distance) const override {
            if(d.z>=-1e-4f||o.z<=-3)return {};
            const float t=(-3-o.z)/d.z;
            return t<=distance?std::optional<RayHit>{RayHit{o+d*t,{0,0,1},5,true}}:std::nullopt;
        }
        bool exists(std::uint64_t) const override { return true; }
    };
    struct Fight {
        WebStrike strike;
        Open open;
        Vec3 feet{};
        std::vector<FightTarget> targets{{7,{0,0,-6},1.8f,.3f,false}};
        Vec3 head() const { return feet+Vec3{0,1.65f,0}; }
        float web{}; // the right hand's web button
        StrikeWish wish; // the enemy marked, and the way the flow calls
        std::optional<WebStrike::Opening> sample(Vec3 fist,Vec3 velocity,bool free,std::uint64_t holds,const WorldQueries* world){
            std::array<FightFist,2> fists{};
            fists[1]={true,fist,velocity,free,holds,web};
            return strike.open(1.f/90,fists,head(),feet,targets,world?*world:static_cast<const WorldQueries&>(open),wish);
        }
        // The fist at rest by the shoulder for `seconds`.
        void rest(float seconds=.5f,bool free=true){
            for(int i=0;i<std::lround(seconds*90);++i)sample(feet+Vec3{.2f,1.4f,-.15f},{},free,0,nullptr);
        }
        // Thrown from the shoulder along `way` at `speed` for a tenth of a second: the strike it opened, if any.
        std::optional<WebStrike::Opening> punch(Vec3 way,float speed,const WorldQueries* world=nullptr,bool free=true,std::uint64_t holds=0){
            std::optional<WebStrike::Opening> opened;
            way=normalized(way);
            for(int i=0;i<=9;++i)
                if(const auto o=sample(feet+Vec3{.2f,1.4f,-.15f}+way*(speed*i/90.f),way*speed,free,holds,world);o&&!opened)opened=o;
            return opened;
        }
        struct Flown { bool landed{},missed{}; float seconds{},fastest{}; };
        // The flight, 90 steps a second; `moving`: the body goes where the strike sends it.
        Flown fly(bool grounded=true,bool moving=true){
            Flown f;
            while(strike.phase()==WebStrike::Phase::Flying&&f.seconds<3){
                const auto s=strike.step(1.f/90,feet,grounded,targets.empty()?nullptr:&targets[0]);
                if(s.drive&&moving)feet+=s.velocity*(1.f/90);
                f.fastest=std::max(f.fastest,length(s.velocity));
                f.landed|=s.landed;f.missed|=s.missed;f.seconds+=1.f/90;
            }
            return f;
        }
    };
    test("web strike: a fist thrown at a thug across the room takes the player to him", [] {
        Fight f;
        f.rest();
        const auto opened=f.punch({0,0,-1},5);
        check(opened&&opened->hand==1&&opened->target==7&&!opened->held,"a punch at a thug six metres off opened no strike");
        near(opened->distance,6,.05f);
        check(f.strike.phase()==WebStrike::Phase::Flying,"the strike did not fly");
        near(f.strike.fistSpeed(),5,.01f);
        const auto flown=f.fly();
        check(flown.landed&&!flown.missed,"the strike's blow did not land");
        // 0.95 m short of him, on his ground, in a fifth of a second at 26 m/s.
        near(f.feet.z,-5.05f,.09f);
        near(f.feet.x,0,.02f);
        near(f.feet.y,0,.02f);
        check(flown.seconds>.15f&&flown.seconds<.25f,"the flight took the wrong time");
        check(flown.fastest>20&&flown.fastest<=26*1.25f+.01f,"the flight went at the wrong speed");
        check(f.strike.phase()==WebStrike::Phase::Idle,"a strike that landed on the ground went on");
    });
    test("web strike: slow, short, wide, busy and drawn-back fists open none", [] {
        const auto opens=[](auto prepare,Vec3 way,float speed,bool free=true){Fight f;f.rest();prepare(f);return f.punch(way,speed,nullptr,free).has_value();};
        const auto same=[](Fight&){};
        check(opens(same,{0,0,-1},5),"the plain case opened nothing");
        check(!opens(same,{0,0,-1},2.3f),"a fist slower than a strike's opened one");
        check(!opens(same,{1,0,-.8f},5),"a fist thrown past him opened a strike");
        check(!opens(same,{0,0,-1},5,false),"a hand whose web holds something opened a strike");
        check(!opens([](Fight& f){f.targets[0].feet={0,0,-1};},{0,0,-1},5),"a thug within the fist's reach was flown at");
        check(!opens([](Fight& f){f.targets[0].feet={0,0,-14};},{0,0,-1},5),"a thug beyond reach was flown at");
        check(!opens([](Fight& f){f.targets.clear();},{0,0,-1},5),"a strike opened at nobody");
        // A hand that let go of its web a moment ago: the throw's follow-through.
        check(!opens([](Fight& f){f.rest(.2f,false);f.rest(.1f);},{0,0,-1},5),"a hand that just let go opened a strike");
        check(opens([](Fight& f){f.rest(.2f,false);f.rest(.4f);},{0,0,-1},5),"a hand free for a while opened none");
        // An arm stretched out ahead, drawn back toward the head, with a thug behind the player's back.
        Fight back;back.rest();back.targets[0].feet={0,0,6};
        std::optional<WebStrike::Opening> opened;
        for(int i=0;i<=6;++i)
            if(const auto o=back.sample({.2f,1.4f,-.75f+5*i/90.f},{0,0,5},true,0,nullptr))opened=o;
        check(!opened,"a hand drawn back opened a strike at the thug behind");
    });
    test("web strike: a twitch opens none, nor a hand that just pressed its web button, unless the web caught him", [] {
        // Three samples at speed: a jolt of the tracking.
        Fight twitch;twitch.rest();
        std::optional<WebStrike::Opening> opened;
        for(int i=0;i<3;++i)
            if(const auto o=twitch.sample({.2f,1.4f,-.15f-5.f*i/90},{0,0,-5},true,0,nullptr))opened=o;
        for(int i=0;i<5;++i)
            if(const auto o=twitch.sample({.2f,1.4f,-.3f},{},true,0,nullptr))opened=o;
        check(!opened,"three fast samples opened a strike");
        // The arm thrown out as the web button goes down: a web shot.
        Fight shot;shot.rest();
        shot.web=1;
        check(!shot.punch({0,0,-1},5),"an arm thrown out to shoot a web opened a strike");
        // Clenched a while ago, the same throw is a punch.
        Fight clenched;clenched.web=1;clenched.rest();
        check(clenched.punch({0,0,-1},5).has_value(),"a fist clenched before the punch opened none");
        // The web that just went out caught him: the throw goes on as a strike along it.
        Fight caught;caught.rest();
        caught.web=1;
        check(caught.punch({0,0,-1},5,nullptr,false,7).has_value(),"a punch along the web that just caught him opened none");
    });
    test("web strike: a player who takes no steps is not flown later, out of nowhere", [] {
        Fight f;f.rest();
        check(f.punch({0,0,-1},5).has_value(),"no strike");
        // Perched: input samples go on, the body takes no step.
        f.rest(.15f);
        check(f.strike.phase()==WebStrike::Phase::Flying,"a strike ended before its player could step");
        f.rest(.25f);
        check(f.strike.phase()==WebStrike::Phase::Idle,"a strike waited for a player who takes no steps");
        check(!f.strike.step(1.f/90,f.feet,true,&f.targets[0]).drive,"a stalled strike flew after all");
        // With steps it flies as long as it takes.
        Fight g;g.rest();
        check(g.punch({0,0,-1},5).has_value(),"no strike");
        for(int i=0;i<10;++i){
            g.strike.step(1.f/90,g.feet,true,&g.targets[0]);
            g.rest(1.f/90);
        }
        check(g.strike.phase()==WebStrike::Phase::Flying,"a stepping strike stalled");
    });
    test("web strike: up from the ground it waits for the jump, its clock standing, and not for ever", [] {
        Fight f;f.rest();
        f.targets[0]={7,{0,3,-2},1.8f,.3f,true};
        check(f.punch({0,.8f,-.6f},5).has_value(),"no strike at a thug overhead");
        check(f.strike.lifting(f.feet),"a strike three metres up needs no jump");
        // Half a second of the game's jump: longer than the stall and than the flight itself.
        for(int i=0;i<45;++i){f.strike.wait(1.f/90);f.rest(1.f/90);}
        check(f.strike.phase()==WebStrike::Phase::Flying,"the strike gave up during the jump");
        near(f.strike.elapsed(),0);
        // In the air now, a metre up: the flight goes on from there.
        f.feet.y=1;
        const auto flown=f.fly(false);
        check(flown.landed,"the strike did not land after the jump");
        near(f.feet.y,3,.09f);
        // A jump that never comes ends it.
        Fight stuck;stuck.rest();
        stuck.targets[0]={7,{0,3,-2},1.8f,.3f,true};
        check(stuck.punch({0,.8f,-.6f},5).has_value(),"no strike");
        for(int i=0;i<70;++i)stuck.strike.wait(1.f/90);
        check(stuck.strike.phase()==WebStrike::Phase::Idle,"a strike waited for ever for a jump");
        // A thug on the same ground, or a step up: no jump.
        Fight level;level.rest();
        check(level.punch({0,0,-1},5).has_value()&&!level.strike.lifting(level.feet),"a strike along the ground asks for a jump");
    });
    test("web strike: of two thugs the one the fist points at, and none behind a wall", [] {
        Fight f;f.rest();
        f.targets={{7,{-2.5f,0,-6},1.8f,.3f,false},{8,{2.5f,0,-6},1.8f,.3f,false}};
        const auto right=f.punch({2.3f,-.5f,-5.85f},5);
        check(right&&right->target==8,"the thug the fist pointed at was not the one struck");
        Fight g;g.rest();
        g.targets={{7,{0,0,-9},1.8f,.3f,false},{8,{0,0,-5},1.8f,.3f,false}};
        const auto nearer=g.punch({0,0,-1},5);
        check(nearer&&nearer->target==8,"of two in line the farther was struck");
        Fight walled;walled.rest();
        const Walled wall;
        check(!walled.punch({0,0,-1},5,&wall),"a strike opened at a thug behind a wall");
        // Nearer than the wall: it stands behind him.
        walled.targets[0].feet={0,0,-2.5f};
        walled.rest();
        check(walled.punch({0,0,-1},5,&wall).has_value(),"a thug in front of the wall was not struck");
    });
    test("web strike: with an enemy marked it is his alone, and a fist swung the way the flow calls opens one at him", [] {
        // Thug 7 six metres ahead; thug 8 off to the right, nearer.
        const auto fight=[]{Fight f;f.targets.push_back({8,{5,0,-2},1.8f,.3f,false});f.rest();return f;};
        const Vec3 atOther{4.8f,-.5f,-1.85f};
        // Nobody marked: the fist picks the one it points at, as ever.
        {
            Fight f=fight();
            const auto o=f.punch(atOther,5);
            check(o&&o->target==8&&f.strike.passedOver()==0,"an unmarked fist did not pick the thug it points at");
        }
        // Thug 7 marked: the same fist flies nowhere, and is counted once for its swing.
        {
            Fight f=fight();f.wish.target=7;
            check(!f.punch(atOther,5)&&f.strike.phase()==WebStrike::Phase::Idle,"a fist at another thug left the marked one");
            check(f.strike.passedOver()==1,"a swing passed over was not counted once");
            f.rest(.2f);
            check(!f.punch(atOther,5)&&f.strike.passedOver()==2,"the next swing was not counted");
        }
        // Thrown between the two, 47 degrees off the marked thug and nearer the other's line: to the marked
        // one. Unmarked, the same fist goes to the other.
        {
            Fight f=fight();f.wish.target=7;
            const auto o=f.punch({1,-.1f,-1},5);
            check(o&&o->target==7&&!o->called&&f.strike.passedOver()==0,"a fist thrown the marked thug's way did not go to him");
            Fight plain=fight();
            const auto p=plain.punch({1,-.1f,-1},5);
            check(p&&p->target==8,"the same fist, unmarked, did not go to the thug it points at best");
        }
        // The marked thug within the fist's own reach: no strike, at him or at anyone.
        {
            Fight f=fight();f.targets[0].feet={0,0,-1};f.wish.target=7;
            check(!f.punch(atOther,5)&&!f.punch({0,0,-1},5),"a strike with the marked thug at arm's length");
        }
        // The flow calls a blow to the left. A hook to the left at the marked thug, across him and a little
        // toward him, opens the strike; unmarked, with nothing called, or the other way, it opens none.
        const Vec3 left{-1,0,0};
        const auto swing=[&](std::uint64_t marked,Vec3 called,Vec3 way){Fight f=fight();f.wish={marked,called};return f.punch(way,5);};
        const auto hook=swing(7,left,{-1,0,-.35f});
        check(hook&&hook->target==7&&hook->called,"a hook the called way opened no strike");
        check(!swing(0,left,{-1,0,-.35f}),"a hook opened a strike with nobody marked");
        check(!swing(7,{},{-1,0,-.35f}),"a hook opened a strike with nothing called");
        check(!swing(7,left,{1,0,-.35f}),"a hook the other way opened a strike");
        // Drawn back from him it is no blow, whatever is called.
        check(!swing(7,left,{-1,0,.5f}),"a fist going away from him opened a strike");
        // An uppercut on a call up, a blow down on a call down.
        check(swing(7,{0,1,0},{0,1,-.45f}).has_value()&&swing(7,{0,-1,0},{0,-1,-.45f}).has_value(),"an uppercut or a blow down, called, opened none");
        // The fist that opened it is the strike's direction: what answers the call.
        Fight f=fight();f.wish={7,left};
        check(f.punch({-1,0,-.35f},5).has_value(),"no strike");
        check(f.strike.direction().x<-.9f,"the strike lost the way its fist went");
        Cues cues;
        cues.call(2,false,false,0);
        check(cues.cue().way==CueWay::Left&&cues.answers(f.strike.direction(),Vec3{0,-.75f,-6}),"the strike's fist does not answer the call it was swung for");
    });
    test("web strike: a punch along the hand's own web strikes the thug it holds, and him alone", [] {
        Fight f;f.rest(.5f,false);
        // Half a right angle off him: wide of a free hand's aim.
        const auto opened=f.punch({1,0,-1.2f},5,nullptr,false,7);
        check(opened&&opened->held&&opened->target==7,"the held thug was not struck");
        Fight other;other.rest(.5f,false);
        check(!other.punch({0,0,-1},5,nullptr,false,9),"a hand holding one thug struck another");
        Fight away;away.rest(.5f,false);
        check(!away.punch({1,0,.3f},5,nullptr,false,7),"a hand swung away from the thug it holds struck him");
    });
    test("web strike: one swing opens one strike, and the next after the fist slowed", [] {
        Fight f;f.rest();
        f.targets.push_back({8,{0,0,-9},1.8f,.3f,false});
        check(f.punch({0,0,-1},5).has_value(),"no strike");
        check(f.fly().landed,"no blow");
        // Still thrown out at 5 m/s as the player arrives, the next thug ahead.
        check(!f.punch({0,0,-1},5),"the same swing opened a second strike");
        f.rest(.2f);
        const auto next=f.punch({0,0,-1},5);
        check(next&&next->target==8,"a new swing did not strike the next thug");
        // A hand that landed a punch by itself opens none with that swing either.
        Fight g;g.rest();
        g.strike.spent(1);
        check(!g.punch({0,0,-1},5),"a swing that landed a punch opened a strike");
    });
    test("web strike: a step to a thug close by takes its time, and a far one is no faster than the flight", [] {
        Fight f;f.rest();f.targets[0].feet={0,0,-2};
        check(f.punch({0,0,-1},5).has_value(),"no strike at a thug two metres off");
        const auto hop=f.fly();
        check(hop.landed&&hop.seconds>=.11f&&hop.fastest<12,"a short hop was a jolt");
        near(f.feet.z,-1.05f,.21f);
        Fight far;far.rest();far.targets[0].feet={0,0,-9.9f};
        check(far.punch({0,0,-1},5).has_value(),"no strike at a thug ten metres off");
        const auto flight=far.fly();
        check(flight.landed&&flight.fastest<=26*1.25f+.01f&&flight.seconds<.45f,"a long flight was wrong");
    });
    test("web strike: a flight that cannot arrive ends; its blow lands only within reach", [] {
        // A railing: the body does not move.
        Fight f;f.rest();
        check(f.punch({0,0,-1},5).has_value(),"no strike");
        const auto stuck=f.fly(true,false);
        check(stuck.missed&&!stuck.landed,"a flight stopped six metres short landed its blow");
        check(stuck.seconds>.3f&&stuck.seconds<.6f,"a stopped flight did not end in time");
        check(f.strike.phase()==WebStrike::Phase::Idle,"a missed strike went on");
        // Stopped a step short of where the player would stand: within reach.
        Fight close;close.rest();close.targets[0].feet={0,0,-2.4f};
        check(close.punch({0,0,-1},5).has_value(),"no strike");
        check(close.fly(true,false).missed,"a blow landed from 2.4 m");
        Fight reach;reach.rest();reach.targets[0].feet={0,0,-1.7f};
        check(reach.punch({0,0,-1},5).has_value(),"no strike");
        check(reach.fly(true,false).landed,"a blow within reach did not land");
        // His actor gone during the flight.
        Fight gone;gone.rest();
        check(gone.punch({0,0,-1},5).has_value(),"no strike");
        gone.strike.step(1.f/90,gone.feet,true,&gone.targets[0]);
        gone.targets.clear();
        const auto lost=gone.fly();
        check(lost.missed&&!lost.landed,"a strike at nobody landed");
        // Cancelled: idle, no blow.
        Fight stop;stop.rest();
        check(stop.punch({0,0,-1},5).has_value(),"no strike");
        stop.strike.cancel();
        check(stop.strike.phase()==WebStrike::Phase::Idle&&!stop.strike.step(1.f/90,stop.feet,true,&stop.targets[0]).drive,
              "a cancelled strike drove the body");
    });
    test("web strike: up to a thug in the air, and the player hangs there after each blow", [] {
        Fight f;f.rest();
        f.targets[0]={7,{0,4,-3},1.8f,.3f,true};
        const auto opened=f.punch({0,.75f,-.66f},5);
        check(opened.has_value(),"no strike at a thug in the air");
        const auto flown=f.fly(false);
        check(flown.landed,"the blow in the air did not land");
        // Beside him, his middle at the player's chest.
        near(f.feet.y,4,.21f);
        near(f.feet.z,-2.05f,.21f);
        check(f.strike.phase()==WebStrike::Phase::Hanging,"the player did not hang after a blow in the air");
        float hung{};
        while(f.strike.phase()==WebStrike::Phase::Hanging&&hung<2){
            const auto s=f.strike.step(1.f/90,f.feet,false,nullptr);
            check(!s.drive||length(s.velocity)==0,"the hang moved the player");
            hung+=1.f/90;
        }
        near(hung,.45f,.03f);
        // Each further blow in the air starts it again, for three seconds in all.
        float total=hung;
        for(int blow=0;blow<20;++blow){
            f.strike.hangOn(false);
            for(int i=0;i<27&&f.strike.phase()==WebStrike::Phase::Hanging;++i){f.strike.step(1.f/90,f.feet,false,nullptr);total+=1.f/90;}
        }
        check(total>2.8f&&total<3.2f,"the hang did not end at its limit");
        check(f.strike.phase()==WebStrike::Phase::Idle,"the hang went on past its limit");
        // On the ground it starts none, and landing ends one and its count.
        f.strike.hangOn(true);
        check(f.strike.phase()==WebStrike::Phase::Idle,"a blow on the ground hung the player");
        f.strike.step(1.f/90,f.feet,true,nullptr);
        f.strike.hangOn(false);
        check(f.strike.phase()==WebStrike::Phase::Hanging,"no hang after landing in between");
        check(!f.strike.step(1.f/90,f.feet,true,nullptr).drive&&f.strike.phase()==WebStrike::Phase::Idle,"the hang held a player on the ground");
    });
    test("web strike: the dive from above lands beside him on his ground", [] {
        Fight f;f.feet={0,8,0};f.rest();
        f.targets[0].feet={0,0,-5};
        check(f.punch({0,-.8f,-.6f},5).has_value(),"no dive at a thug below");
        const auto dive=f.fly(false);
        check(dive.landed,"the dive's blow did not land");
        near(f.feet.y,0,.21f);
        near(f.feet.z,-4.05f,.21f);
    });
    test("flow: blows in a row count up to a finisher, and a pause starts over", [] {
        Flow flow;
        for(int i=1;i<=4;++i){
            check(flow.hit()==i&&!flow.finisher(i),"the flow miscounted");
            flow.update(1);
        }
        check(flow.hit()==5&&flow.finisher(5)&&!flow.finisher(6)&&flow.finisher(10),"the fifth blow was no finisher");
        near(flow.damage(1),1);
        near(flow.damage(3),1.2f);
        near(flow.damage(40),1.5f);
        flow.update(3.4f);
        check(flow.count()==5,"the flow ended early");
        flow.update(.2f);
        check(flow.count()==0&&flow.hit()==1,"the flow went on after a pause");
        bool refused{};
        try{FlowConfig c;c.finisher=1;Flow bad(c);}catch(const std::invalid_argument&){refused=true;}
        check(refused,"a finisher on every blow accepted");
    });
    test("blows: light ones are the game's melee; hard ones, uppercuts, air blows and finishers send him flying", [] {
        const Punches punches;
        const Flow flow;
        const auto blow=[&](Vec3 way,float speed){return punches.blow(way,speed);};
        const auto jab=deliver(blow({0,0,-1},3),false,1,flow);
        check(jab.kind==BlowKind::Hit&&!jab.kinetic&&!jab.fly&&jab.knockback==Knockback::Stagger,"a jab was no plain hit");
        near(jab.damage,blow({0,0,-1},3).damage);
        // The same jab third in a flow: a fifth harder.
        near(deliver(blow({0,0,-1},3),false,3,flow).damage,blow({0,0,-1},3).damage*1.2f);
        const auto hard=deliver(blow({0,0,-1},7),false,1,flow),harder=deliver(blow({0,0,-1},9),false,1,flow);
        check(hard.kind==BlowKind::Heavy&&hard.kinetic&&hard.fly&&hard.knockback==Knockback::FlyBack,"a hard blow did not send him flying");
        check(hard.velocity.z<-9&&hard.velocity.y>2.9f&&harder.velocity.z<hard.velocity.z-2,"a hard blow's flight was wrong");
        near(harder.velocity.z,-14,.01f);
        const auto upper=deliver(blow({0,.9f,-.44f},7),false,1,flow);
        check(upper.kind==BlowKind::Launch&&upper.kinetic&&upper.velocity.y>8&&std::abs(upper.velocity.z)<2,"an uppercut did not lift him");
        const auto juggle=deliver(blow({0,0,-1},3),true,1,flow);
        check(juggle.kind==BlowKind::Juggle&&juggle.fly&&juggle.velocity.y>4&&juggle.velocity.z<-2.9f,"a blow at a thug in the air did not keep him up");
        const auto slam=deliver(blow({0,-.9f,-.44f},5),true,1,flow);
        check(slam.kind==BlowKind::Slam&&slam.velocity.y<-20,"a blow down at a thug in the air did not slam him");
        near(slam.damage,blow({0,-.9f,-.44f},5).damage*1.6f);
        const auto hammer=deliver(blow({0,-.9f,-.44f},5),false,1,flow);
        check(hammer.kind==BlowKind::Hit&&!hammer.kinetic&&hammer.knockback==Knockback::Knockdown,"a blow down onto a standing thug did not knock him down");
        // The fifth of a flow, whatever the blow: far, and two and a half times as hard on top of the flow's 1.4.
        const auto finish=deliver(blow({0,0,-1},3),false,5,flow);
        check(finish.kind==BlowKind::Finisher&&finish.kinetic&&finish.velocity.z<-17&&finish.velocity.y>4.9f,"the finisher was weak");
        near(finish.damage,blow({0,0,-1},3).damage*1.4f*2.5f,.01f);
        check(deliver(blow({0,-.9f,-.44f},5),true,5,flow).kind==BlowKind::Finisher,"a finisher in the air was something else");
        near(speedOf(punches.config(),.5f),4.85f);
    });
    test("hit stop: the game's time stops for a blow and eases back, longer for a finisher", [] {
        HitStop stop;
        near(stop.timeScale(),1);
        check(!stop.active(),"a stop before any blow");
        stop.hit(1,false);
        near(stop.timeScale(),.1f);
        stop.update(.09f);
        near(stop.timeScale(),.1f);
        stop.update(.03f);
        check(stop.timeScale()>.1f&&stop.timeScale()<.5f,"the stop did not ease back");
        float before=stop.timeScale();
        for(int i=0;i<14;++i){stop.update(.01f);check(stop.timeScale()>=before,"time went back down");before=stop.timeScale();}
        near(stop.timeScale(),1);
        check(!stop.active(),"the stop went on");
        // A jab's is a third as long.
        stop.hit(0,false);
        stop.update(.035f);
        check(stop.timeScale()>.1f,"a jab stopped time as long as a full blow");
        stop.reset();
        // A finisher: longer, less deep; a jab landed in it does not cut it short.
        stop.hit(.3f,true);
        near(stop.timeScale(),.2f);
        stop.update(.2f);
        stop.hit(0,false);
        stop.update(.15f);
        near(stop.timeScale(),.2f);
        stop.update(.3f);
        near(stop.timeScale(),1);
        // A hard blow as a jab's stop ends takes over.
        stop.hit(0,false);
        stop.update(.02f);
        stop.hit(1,false);
        stop.update(.05f);
        near(stop.timeScale(),.1f);
        stop.reset();
        near(stop.timeScale(),1);
        bool refused{};
        try{HitStopTuning t;t.scale=0;HitStop bad(t);}catch(const std::invalid_argument&){refused=true;}
        check(refused,"a stop that halts the game accepted");
    });
    test("web strike configuration rejects invalid tuning", [] {
        auto bad=[](auto change){WebStrikeConfig c;change(c);try{WebStrike s(c);return false;}catch(const std::invalid_argument&){return true;}};
        check(bad([](WebStrikeConfig& c){c.maxDistance=1;}),"a reach inside the fist's own accepted");
        check(bad([](WebStrikeConfig& c){c.speed=0;}),"a flight at no speed accepted");
        check(bad([](WebStrikeConfig& c){c.rearmSpeed=3;}),"a rearm speed above a strike's accepted");
        check(bad([](WebStrikeConfig& c){c.aim=std::numeric_limits<float>::quiet_NaN();}),"NaN aim accepted");
        check(bad([](WebStrikeConfig& c){c.heldAim=.9f;}),"a held thug harder to aim at than a free one accepted");
        check(!bad([](WebStrikeConfig&){}),"defaults rejected");
    });
    test("impact sounds: each a WAV in memory, the harder the blow the longer and louder", [] {
        const auto samples=[](const std::vector<std::uint8_t>& wave){
            std::vector<float> out;
            for(size_t i=44;i+1<wave.size();i+=2)out.push_back(static_cast<std::int16_t>(wave[i]|wave[i+1]<<8)/32767.f);
            return out;
        };
        const auto u32=[](const std::vector<std::uint8_t>& w,size_t at){return static_cast<std::uint32_t>(w[at]|w[at+1]<<8|w[at+2]<<16|w[at+3]<<24);};
        float lastPeak{},lastEnergy{};
        size_t lastCount{};
        for(const auto sound:{ImpactSound::Light,ImpactSound::Firm,ImpactSound::Heavy,ImpactSound::Finisher}){
            const auto wave=impactWave(sound);
            check(wave.size()>44&&!std::memcmp(wave.data(),"RIFF",4)&&!std::memcmp(wave.data()+8,"WAVEfmt ",8)&&!std::memcmp(wave.data()+36,"data",4),"no WAV header");
            check(u32(wave,4)==wave.size()-8&&u32(wave,40)==wave.size()-44&&u32(wave,24)==22050&&u32(wave,28)==44100,"the WAV's sizes are wrong");
            const auto s=samples(wave);
            near(static_cast<float>(s.size())/22050,impactSeconds(sound),.001f);
            float peak{},energy{},sum{};
            for(const float v:s){peak=std::max(peak,std::abs(v));energy+=v*v;sum+=v;}
            check(peak>.3f&&peak<.9f,"a blow's sound is too quiet or clipped");
            check(peak>lastPeak&&energy>lastEnergy&&s.size()>lastCount,"a harder blow was not louder and longer");
            check(std::abs(s.front())<.01f&&std::abs(s.back())<.01f,"the sound clicks at an end");
            check(std::abs(sum/s.size())<.02f,"the sound is off centre");
            // The smack comes first: the first 20 ms hold more than their share.
            float early{};
            for(size_t i=0;i<441;++i)early+=s[i]*s[i];
            check(early>energy*441/s.size(),"the blow has no attack");
            lastPeak=peak;lastEnergy=energy;lastCount=s.size();
        }
        const auto rush=samples(impactWave(ImpactSound::Rush));
        float peak{};
        size_t loudest{};
        for(size_t i=0;i<rush.size();++i)if(std::abs(rush[i])>peak){peak=std::abs(rush[i]);loudest=i;}
        check(peak>.05f&&peak<.45f,"the rush is too quiet or too loud");
        check(loudest>rush.size()/5&&loudest<rush.size()*4/5,"the rush does not swell and fade");
        check(impactWave(ImpactSound::Light)==impactWave(ImpactSound::Light),"the same sound came out differently");
        check(impactWave(ImpactSound::Firm,1).size()==44+2*static_cast<size_t>(std::lround(.16f*8000)),"an absurd rate was not limited");
    });
    test("impact burst is printed on twos: a star small and then whole, its lines thrown clear, the flow's count lettered over it, then nothing", [] {
        const Vec3 viewer{0,1.6f,0},at{0,1.4f,-.7f};
        const float pose=1.f/12;
        const auto draw=[&](float age,BlowKind kind=BlowKind::Hit,int count=1,float strength=.5f){
            std::vector<Vertex> out;
            appendImpact(out,{at,strength,kind,count,age},viewer,.0006f);
            return out;
        };
        const Vec3 ink{.0033f,.0030f,.0070f},paper{.9301f,.9131f,.8550f},cyan{.0060f,.6724f,1},magenta{1,.0273f,.2918f},
            yellow{1,.7529f,.0742f},red{.7682f,.0194f,.0284f};
        const auto count=[](const std::vector<Vertex>& v,Vec3 plate) {
            size_t n{};
            for(const auto& x:v)n+=length(x.color-plate)<.01f;
            return n;
        };
        const auto widest=[&](const std::vector<Vertex>& v){
            float r{};
            for(const auto& p:v)r=std::max(r,length(p.position-at));
            return r;
        };
        const auto same=[](const std::vector<Vertex>& a,const std::vector<Vertex>& b) {
            if(a.size()!=b.size())return false;
            for(size_t i=0;i<a.size();++i)
                if(length(a[i].position-b[i].position)>0||length(a[i].color-b[i].color)>0||a[i].alpha!=b[i].alpha)return false;
            return true;
        };
        // A jab: a yellow star around paper, keyed in ink, small and then whole; then its paper lines, clear of the middle.
        const auto first=draw(pose*.5f),second=draw(pose*1.5f),third=draw(pose*2.5f);
        check(!first.empty()&&first.size()%3==0,"no burst");
        const Vec3 facing=normalized(viewer-at);
        for(const auto& v:first)check(finite(v.position)&&v.alpha>=0&&v.alpha<=1&&std::abs(dot(v.position-at,facing))<1e-3f,"a burst vertex off its plane");
        check(count(first,yellow)>0&&count(first,paper)>0&&count(first,ink)>0&&count(first,red)==0,"a jab's star is not yellow around paper, keyed in ink");
        check(widest(second)>widest(first)*1.15f,"the star does not grow");
        check(widest(third)>widest(second)&&widest(third)<.35f,"the lines are not thrown past the star, or too far");
        check(count(third,yellow)==0&&count(third,paper)>0&&count(third,ink)>0,"a jab's lines are not paper keyed in ink");
        for(const auto& v:third)check(length(v.position-at)>.11f*.6f,"a line across the middle");
        // On twos: a pose is held, then the next.
        check(same(draw(.01f),draw(.07f))&&!same(draw(.07f),draw(.09f)),"a pose is not held a twelfth of a second");
        check(draw(.26f).empty()&&draw(-.01f).empty()&&draw(1).empty(),"a burst outside its time");
        // A harder blow's is red around yellow, with a pose of lines more.
        const auto heavy=draw(pose*1.5f,BlowKind::Heavy);
        check(count(heavy,red)>0&&count(heavy,yellow)>0&&widest(heavy)>widest(second),"a harder blow's star is not red around yellow, or no wider");
        check(!draw(pose*3.5f,BlowKind::Heavy).empty()&&draw(pose*4.5f,BlowKind::Heavy).empty(),"a harder blow's lines do not last two poses");
        // A finisher's is wider and longer: a third star, three poses of lines.
        const auto finisher=draw(pose*1.5f,BlowKind::Finisher);
        check(count(finisher,red)>0&&widest(finisher)>widest(heavy)*1.1f&&widest(finisher)<.2f,"a finisher's burst is no bigger, fills the view, or is not red");
        check(count(draw(pose*2.5f,BlowKind::Finisher),red)>0&&!draw(pose*4.5f,BlowKind::Finisher).empty()&&draw(pose*5.5f,BlowKind::Finisher).empty(),
              "a finisher's burst is no longer");
        // The count shows from the second blow on, above the blow, and outlasts the burst.
        const auto counted=draw(pose*5.5f,BlowKind::Hit,3);
        check(!counted.empty()&&count(counted,yellow)>0&&count(counted,ink)>0,"no count after the burst");
        for(const auto& v:counted)check(v.position.y>1.4f,"the count is not above the blow");
        check(draw(pose*5.5f,BlowKind::Hit,1).empty(),"a first blow shows a count");
        check(draw(.76f,BlowKind::Hit,3).empty(),"the count stayed");
        // Its lettering (above a light jab's star) lands small, then past its size and out of register; holds;
        // and leaves in two poses, large and out of register, then small. Nothing of it fades.
        const auto lettering=[&](int i){
            std::vector<Vertex> v;
            for(const auto& x:draw(pose*(static_cast<float>(i)+.5f),BlowKind::Hit,3,0))if(x.position.y>1.465f)v.push_back(x);
            return v;
        };
        const auto wide=[&](const std::vector<Vertex>& v){
            float low=1e9f,high=-1e9f;
            for(const auto& x:v)if(length(x.color-yellow)<.01f){low=std::min(low,x.position.x);high=std::max(high,x.position.x);}
            return high-low;
        };
        const float held=wide(lettering(5));
        check(held>.03f&&held<.2f,"no lettering, or too large");
        check(wide(lettering(0))<held*.5f&&wide(lettering(1))>held*1.05f&&count(lettering(1),magenta)>0&&count(lettering(1),cyan)>0,"the count does not pop in");
        check(count(lettering(5),magenta)+count(lettering(5),cyan)==0&&same(lettering(4),lettering(5)),"the count does not hold");
        check(wide(lettering(7))>held*1.05f&&count(lettering(7),cyan)>0&&wide(lettering(8))<held*.7f,"the count does not leave in two poses");
        float full{};
        for(const auto& x:lettering(8))full=std::max(full,x.alpha);
        check(full==1,"the count fades");
        // Nothing for nonsense.
        std::vector<Vertex> none;
        appendImpact(none,{{0,1.4f,-.7f},.5f,BlowKind::Hit,1,.02f},{0,1.4f,-.7f},.0006f);
        appendImpact(none,{{std::numeric_limits<float>::quiet_NaN(),0,0},.5f,BlowKind::Hit,1,.02f},viewer,.0006f);
        check(none.empty(),"a burst at the viewer's eye or nowhere");
    });
    // The right hand at eye height, aimed along -z (an identity aim pose), its trigger at `trigger`.
    const auto shooterHands = [](float trigger, bool busy = false, bool tracked = true) {
        std::array<ShooterHand, 2> hands{};
        hands[1] = {tracked, {{0, 1.5f, 0}, {}}, trigger, busy};
        return hands;
    };
    test("web shooter: a pull shoots once, a held trigger never again, a fresh pull again", [=] {
        Shooter s;
        const float dt = 1.f / 90;
        check(s.update(dt, shooterHands(0)) == 0, "an open trigger shot");
        check(s.update(dt, shooterHands(1)) == 2, "a pull did not shoot from the right hand");
        for (int i = 0; i < 30; ++i)
            check(s.update(dt, shooterHands(1)) == 0, "a held trigger shot again");
        check(s.update(dt, shooterHands(.5f)) == 0 && s.update(dt, shooterHands(.9f)) == 0,
              "easing the trigger halfway counted as a release");
        check(s.update(dt, shooterHands(.2f)) == 0 && s.update(dt, shooterHands(.7f)) == 2, "a fresh pull did not shoot");
        check(s.update(dt, shooterHands(0)) == 0 && s.update(0, shooterHands(1)) == 0,
              "the same sample again shot");
        auto open = shooterHands(0), both = shooterHands(1);
        open[0] = open[1];
        both[0] = both[1];
        s.update(.2f, open);
        check(s.update(dt, both) == 3, "both hands pulled together did not both shoot");
    });
    test("web shooter: pulls closer than its interval shoot once", [=] {
        Shooter s;
        s.update(.03f, shooterHands(0));
        check(s.update(.03f, shooterHands(1)) == 2, "the first pull");
        s.update(.03f, shooterHands(0));
        check(s.update(.03f, shooterHands(1)) == 0, "a pull 60 ms later shot");
        s.update(.05f, shooterHands(0));
        check(s.update(.05f, shooterHands(1)) == 2, "a pull 160 ms after the shot did not shoot");
    });
    test("web shooter: a busy hand reels, and a trigger held from then or through a tracking loss is no pull", [=] {
        Shooter s;
        const float dt = 1.f / 90;
        check(s.update(dt, shooterHands(1)) == 0, "a trigger already pulled at the start shot");
        s.update(dt, shooterHands(0));
        check(s.update(dt, shooterHands(1, true)) == 0, "a hand whose web holds something shot");
        check(s.update(dt, shooterHands(1)) == 0, "a trigger held after its web let go shot");
        s.update(dt, shooterHands(0));
        s.update(dt, shooterHands(1, false, false));
        check(s.update(dt, shooterHands(1)) == 0, "a trigger held through a tracking loss shot");
        s.update(dt, shooterHands(0));
        check(s.update(dt, shooterHands(1)) == 2, "a fresh pull after all that did not shoot");
        s.reset();
        check(s.update(dt, shooterHands(1)) == 0, "a trigger held through a reset shot");
    });
    test("web shooter aims at a thug near its line, never through a wall, else at the surface or open air", [] {
        // A wall across the line 30 m ahead, and a pillar 10 m ahead that can stand right of the line.
        struct Range : WorldQueries {
            bool pillar{};
            std::optional<RayHit> raycast(Vec3 o, Vec3 d, float distance) const override {
                if (d.z >= 0)
                    return {};
                std::optional<RayHit> hit;
                const float pillarAt = (o.z + 10) / -d.z;
                const Vec3 p = o + d * pillarAt;
                if (pillar && pillarAt > 0 && pillarAt <= distance && p.x > .5f && p.x < 1.5f)
                    return RayHit{p, {0, 0, 1}, 2, true};
                const float wallAt = (o.z + 30) / -d.z;
                if (wallAt > 0 && wallAt <= distance)
                    hit = RayHit{o + d * wallAt, {0, 0, 1}, 1, true};
                return hit;
            }
            bool exists(std::uint64_t) const override {
                return true;
            }
        } world;
        Shooter s;
        const Pose hand{{0, 1.5f, 0}, {}};
        auto shot = s.aim(1, hand, {}, world);
        near(shot.origin.z, -ShooterConfig{}.muzzle);
        check(!shot.target && shot.surface && shot.hand == 1, "no thug: the wall");
        near(shot.aimPoint.z, -30.4f);
        near(shot.aimPoint.y, 1.5f);
        const float up = 1.5707964f;
        shot = s.aim(1, {{0, 1.5f, 0}, {std::sin(up / 2), 0, 0, std::cos(up / 2)}}, {}, world);
        check(!shot.surface && !shot.target, "the sky is open air");
        near(shot.aimPoint.y, 1.5f + ShooterConfig{}.muzzle + 60, .01f);
        // A thug 2.9 degrees off the line, one 14 degrees off, one nearly on it but behind the wall, one
        // behind the hand and one beyond the assist's range.
        const ShooterTarget near1{11, {1, 1.5f, -20}}, wide{12, {5, 1.5f, -20}}, walled{13, {-.5f, 1.5f, -40}},
            behind{14, {0, 1.5f, 5}}, far{15, {0, 1.5f, -50}};
        const ShooterTarget all[] = {wide, walled, behind, far, near1};
        shot = s.aim(1, hand, all, world);
        check(shot.target == 11, "the thug near the line was not the one");
        near(shot.aimPoint.x, 1);
        near(length(shot.direction - normalized(near1.centre - shot.origin)), 0);
        const ShooterTarget others[] = {wide, walled, behind, far};
        check(!s.aim(1, hand, others, world).target, "a thug off the line, behind a wall, behind or too far was taken");
        // Close by, a thug is taken anywhere within his own width of the line.
        const ShooterTarget close[] = {{16, {.4f, 1.5f, -2}}};
        check(s.aim(1, hand, close, world).target == 16, "a thug beside the hand was not taken");
        const ShooterTarget beside[] = {{17, {.8f, 1.5f, -2}}};
        check(!s.aim(1, hand, beside, world).target, "a thug clear of the line was taken");
        // A pillar between the hand and the thug takes the ball first.
        world.pillar = true;
        const ShooterTarget hidden[] = {{18, {2, 1.5f, -20}}};
        auto blocked = s.aim(1, {{0, 1.5f, 0}, Quat::yaw(-std::atan2(2.f, 19.92f))}, hidden, world);
        check(!blocked.target && blocked.surface, "a thug behind the pillar was taken");
        near(blocked.aimPoint.z, -10.4f, .05f);
    });
    test("web shooter configuration rejects invalid tuning", [] {
        auto bad = [](auto change) {
            ShooterConfig c;
            change(c);
            try {
                Shooter s(c);
                return false;
            } catch (const std::invalid_argument&) {
                return true;
            }
        };
        check(bad([](ShooterConfig& c) { c.release = c.press; }), "a release no lower than the pull accepted");
        check(bad([](ShooterConfig& c) { c.range = 0; }), "no range accepted");
        check(bad([](ShooterConfig& c) { c.assistAngle = std::numeric_limits<float>::quiet_NaN(); }),
              "NaN assist accepted");
        check(!bad([](ShooterConfig&) {}), "defaults rejected");
    });
    test("VR settings tab: six sections, every setting once, a list's choices one per step", [] {
        using namespace vr_settings;
        const auto& all = rows();
        unsigned seen = 0, headings = 0;
        for (const auto& row : all) {
            check(row.title && *row.title, "a row without a title");
            for (const char* c = row.title; *c; ++c)
                check(!(*c >= 'a' && *c <= 'z'), "a title not in upper case, as the game's own");
            if (row.item == Item::none) {
                check(!row.help && row.choices.empty(), "a heading with help or choices");
                ++headings;
                continue;
            }
            const unsigned bit = 1u << static_cast<unsigned>(row.item);
            check(!(seen & bit), "a setting twice");
            seen |= bit;
            check(row.help && std::strlen(row.help) <= 80, "help missing, or too long for the game's column");
            for (const char* c : row.choices)
                check(c && *c, "an empty choice");
        }
        // The sections (the player, October 19: "sort the options in spidy vr
        // settings ingame into nice sections"), each with a setting in the tab.
        const std::pair<size_t, const char*> sections[] = {{10, "WEBS"},     {15, "SWINGING"}, {18, "COMFORT"},
                                                           {22, "DISPLAY"}, {27, "BODY"},     {29, "EXPERIMENTAL"}};
        check(headings == std::size(sections), "the sections: webs, swinging, comfort, display, body, experimental");
        for (const auto& [at, title] : sections) {
            check(all[at].item == Item::none && std::strcmp(all[at].title, title) == 0, "a section out of place");
            size_t next = at + 1;
            while (next < all.size() && all[next].page)
                ++next;
            check(next < all.size() && all[next].item != Item::none, "a section with no setting in the tab");
        }
        check(all[30].item == Item::flips && all[30].choices.empty() && std::strcmp(all[29].title, "EXPERIMENTAL") == 0,
              "the flips: a switch under EXPERIMENTAL");
        check(all.size() == 32 && all[31].item == Item::flipSpeed && all[31].choices.size() == std::size(flipSpeeds),
              "the flip speed under the flips, the last row");
        // The wall view has no row of its own any more: it turned only while
        // Spidy held the player on a wall, so it was never a choice of its own
        // and the launcher's one "Wall running" now carries both (the player,
        // October 14: "i dont get why we have a wall run and a wall stand
        // options where wall run actually controlls wheter i stand or crawl",
        // then "also merge the two options").
        for (const auto& row : all)
            check(row.item != Item::standOnWalls, "the wall view still has a row of its own");
        check(all[24].item == Item::screenSize && all[26].item == Item::hud && all[26].choices.size() == 4 &&
                  std::strcmp(all[26].choices[0], "OFF") == 0 && std::strcmp(all[26].choices[3], "LARGE") == 0,
              "the HUD after the game screen's rows: OFF, SMALL, MEDIUM, LARGE");
        check(all[25].item == Item::screenDepth && std::strcmp(all[25].title, "GAME SCREEN 3D") == 0 &&
                  all[25].choices.size() == 4 && std::strcmp(all[25].choices[0], "OFF") == 0 &&
                  std::strcmp(all[25].choices[3], "HIGH") == 0,
              "the game screen's depth under its size: OFF, LOW, MEDIUM, HIGH");
        // Webs are always physical (the user, October 15: "remove non physical web, no need for it").
        for (const auto& row : all)
            check(std::strcmp(row.title, "PHYSICAL WEBS") != 0, "webs can still be made non-physical");
        check(all[23].item == Item::view && std::strcmp(all[23].title, "VR VIEW") == 0 && all[23].choices.size() == 2 &&
                  std::strcmp(all[23].choices[0], "VR") == 0 && std::strcmp(all[23].choices[1], "GAME SCREEN") == 0,
              "the view above the game screen's rows: VR, or GAME SCREEN");
        // VR VIEW is on the pause menu's first page, beside RESUME, and no other row is.
        for (const auto& row : all)
            check(row.page == (row.item == Item::view), "a row on the wrong page");
        check(all[11].item == Item::webStyle && std::strcmp(all[11].title, "WEB STYLE") == 0 &&
                  all[11].choices.size() == 2 && std::strcmp(all[11].choices[0], "REALISTIC") == 0 &&
                  std::strcmp(all[11].choices[1], "ARCADE") == 0,
              "the web style first under WEBS: REALISTIC, or ARCADE");
        check(all[12].item == Item::webButton && all[12].choices.size() == 2 &&
                  std::strcmp(all[12].choices[0], "GRIP") == 0 && std::strcmp(all[12].choices[1], "TRIGGER") == 0,
              "the web button under the web style: GRIP, or TRIGGER");
        check(all[13].item == Item::aimMarkers && all[14].item == Item::airWebs, "the aim markers, then the air webs");
        check(all[16].item == Item::swingSpeed && all[17].item == Item::weight, "SWINGING: the swing speed, the weight");
        check(all[19].item == Item::snapTurn && all[20].item == Item::smoothTurn && all[21].item == Item::haptics,
              "COMFORT: snap and smooth turning, the vibration");
        check(all[28].item == Item::calibrate && all[28].choices.size() == 2 &&
                  std::strcmp(all[28].choices[1], "ON RESUME") == 0,
              "the body's calibration: NO, or ON RESUME");
        // Every setting has a row but the wall view, which the launcher's one
        // "Wall running" now carries along with who holds you on the wall.
        constexpr unsigned everySetting = (2u << static_cast<unsigned>(lastItem)) - 2;
        constexpr unsigned noRowOfItsOwn = 1u << static_cast<unsigned>(Item::standOnWalls);
        check(seen == (everySetting & ~noRowOfItsOwn), "a setting missing");
        auto choicesOf = [&](Item item) {
            return std::find_if(all.begin(), all.end(), [&](const Row& r) { return r.item == item; })->choices.size();
        };
        check(choicesOf(Item::swingSpeed) == std::size(swingSpeeds) && choicesOf(Item::snapTurn) == std::size(snapTurns) &&
                  choicesOf(Item::smoothTurn) == std::size(smoothTurns) &&
                  choicesOf(Item::haptics) == std::size(hapticLevels) && choicesOf(Item::screenSize) == 3 &&
                  choicesOf(Item::weight) == std::size(weights) &&
                  choicesOf(Item::airWebs) == 0 && choicesOf(Item::aimMarkers) == 0,
              "lists: one choice per step; switches: the game's own ON and OFF");
        check(choicesOf(Item::calibrate) == 2, "the calibration's switch names its two choices");
        check(all[0].item == Item::realisticFlips && all[9].item == Item::automaticAirBrake,
              "realistic rotation settings appear first");
    });
    test("VR settings choices: switches and lists round-trip, launcher values show the nearest step", [] {
        using namespace vr_settings;
        const Values defaults;
        check(choice(Item::aimMarkers, defaults) == 1 && choice(Item::swingSpeed, defaults) == 4 &&
                  choice(Item::snapTurn, defaults) == 2 && choice(Item::smoothTurn, defaults) == 0 &&
                  choice(Item::haptics, defaults) == 4 && choice(Item::screenSize, defaults) == 1 &&
                  choice(Item::weight, defaults) == 3,
              "the defaults as the tab shows them: ON, 32 m/s, 30 degrees, no smooth turning, 100%, medium, 100%");
        check(defaults.weight == 100, "the default weight is real gravity");
        Values v;
        check(choose(Item::swingSpeed, 5, v) && v.swingSpeed == 40, "faster");
        check(!choose(Item::swingSpeed, 5, v), "the same choice changed something");
        check(!choose(Item::swingSpeed, -1, v) && !choose(Item::swingSpeed, 9, v) && v.swingSpeed == 40,
              "a choice past either end");
        v.swingSpeed = 33;
        check(choice(Item::swingSpeed, v) == 4, "a launcher value shows the step nearest to it");
        v.swingSpeed = 36;
        check(choice(Item::swingSpeed, v) == 4, "halfway shows the lower step");
        v.swingSpeed = 37;
        check(choice(Item::swingSpeed, v) == 5, "past halfway shows the higher step");
        check(choose(Item::snapTurn, 0, v) && v.snapTurn == 0 && choice(Item::snapTurn, v) == 0, "snap turning off");
        check(choose(Item::smoothTurn, 3, v) && v.smoothTurn == 120 && choice(Item::smoothTurn, v) == 3 &&
                  !choose(Item::smoothTurn, 6, v),
              "smooth turning at 120 degrees a second; 240 is the last");
        v.smoothTurn = 100;
        check(choice(Item::smoothTurn, v) == 2, "a launcher speed shows the step nearest to it");
        check(choose(Item::haptics, 1, v) && v.haptics == 25, "a quarter of the vibration");
        check(!choose(Item::weight, 3, v) && choose(Item::weight, 2, v) && v.weight == 80 &&
                  choose(Item::weight, 3, v) && v.weight == 100 && choice(Item::weight, v) == 3,
              "real gravity, the default, and lighter");
        check(choose(Item::weight, 8, v) && v.weight == 300 && !choose(Item::weight, 9, v) &&
                  choose(Item::weight, 0, v) && v.weight == 40 && !choose(Item::weight, -1, v),
              "300% is the heaviest, 40% the lightest");
        v.weight = 70;
        check(choice(Item::weight, v) == 1, "a launcher weight halfway shows the lighter step");
        v.weight = 71;
        check(choice(Item::weight, v) == 2, "past halfway, the heavier one");
        near(gravity(100), 9.81f);
        near(gravity(300), 29.43f);
        check(choose(Item::screenSize, 2, v) && v.screenSize == 2 && !choose(Item::screenSize, 3, v),
              "the large screen is the last");
        check(choose(Item::airWebs, 0, v) && !v.airWebs && choice(Item::airWebs, v) == 0 &&
                  !choose(Item::airWebs, 2, v) && choose(Item::airWebs, 1, v) && v.airWebs,
              "a switch switches back, and takes only OFF and ON");
        check(choose(Item::aimMarkers, 0, v) && !v.aimMarkers, "the aim markers off");
        check(choice(Item::calibrate, defaults) == 0 && defaultChoice(Item::calibrate) == 0 &&
                  choose(Item::calibrate, 1, v) && v.calibrate && choice(Item::calibrate, v) == 1 &&
                  !choose(Item::calibrate, 2, v),
              "a calibration asked for: ON RESUME, after NO by default");
        check(!defaults.flips && choice(Item::flips, defaults) == 0 && defaultChoice(Item::flips) == 0 &&
                  choose(Item::flips, 1, v) && v.flips && choice(Item::flips, v) == 1,
              "the experimental flips: OFF by default, a switch to ON");
        check(!defaults.flatScreen && choice(Item::view, defaults) == 0 && defaultChoice(Item::view) == 0 &&
                  choose(Item::view, 1, v) && v.flatScreen && choice(Item::view, v) == 1 &&
                  !choose(Item::view, 2, v) && choose(Item::view, 0, v) && !v.flatScreen,
              "the view: VR by default, GAME SCREEN the other choice");
        check(defaults.standOnWalls && choice(Item::standOnWalls, defaults) == 1 &&
                  defaultChoice(Item::standOnWalls) == 1 && choose(Item::standOnWalls, 0, v) && !v.standOnWalls &&
                  !choose(Item::standOnWalls, 0, v) && choose(Item::standOnWalls, 1, v) && v.standOnWalls,
              "standing on walls: ON by default, a switch");
        check(!defaults.triggerWebs && choice(Item::webButton, defaults) == 0 && defaultChoice(Item::webButton) == 0 &&
                  choose(Item::webButton, 1, v) && v.triggerWebs && choice(Item::webButton, v) == 1 &&
                  !choose(Item::webButton, 1, v) && !choose(Item::webButton, 2, v),
              "the web button: GRIP by default, TRIGGER swaps it");
        check(!defaults.arcadeWebs && choice(Item::webStyle, defaults) == 0 && defaultChoice(Item::webStyle) == 0 &&
                  choose(Item::webStyle, 1, v) && v.arcadeWebs && choice(Item::webStyle, v) == 1 &&
                  !choose(Item::webStyle, 1, v) && !choose(Item::webStyle, 2, v),
              "the web style: REALISTIC by default, ARCADE the other");
        check(defaults.hud == 2 && choice(Item::hud, defaults) == 2 && defaultChoice(Item::hud) == 2 &&
                  choose(Item::hud, 0, v) && v.hud == 0 && choice(Item::hud, v) == 0 && choose(Item::hud, 3, v) &&
                  v.hud == 3 && !choose(Item::hud, 4, v) && !choose(Item::hud, -1, v),
              "the HUD: MEDIUM by default, OFF to LARGE");
        check(defaults.screenDepth == 2 && choice(Item::screenDepth, defaults) == 2 &&
                  defaultChoice(Item::screenDepth) == 2 && choose(Item::screenDepth, 0, v) && v.screenDepth == 0 &&
                  choose(Item::screenDepth, 3, v) && v.screenDepth == 3 && !choose(Item::screenDepth, 4, v) &&
                  !choose(Item::screenDepth, -1, v),
              "the game screen's depth: MEDIUM by default, OFF to HIGH");
        near(screenDepth(0), 0);
        near(screenDepth(2), .7f);
        near(screenDepth(3), 1);
        near(screenDepth(9), 1);
        Values deep;
        deep.screenDepth = 9;
        check(sanitized(deep).screenDepth == 3, "a depth past HIGH");
        check(defaults.flipSpeed == 180 && choice(Item::flipSpeed, defaults) == 3 &&
                  defaultChoice(Item::flipSpeed) == 3 && choose(Item::flipSpeed, 4, v) && v.flipSpeed == 240 &&
                  choose(Item::flipSpeed, 0, v) && v.flipSpeed == 90 && choose(Item::flipSpeed, 7, v) &&
                  v.flipSpeed == 480 && !choose(Item::flipSpeed, 8, v) && !choose(Item::flipSpeed, -1, v),
              "the flip speed: 180 degrees a second by default, 90 to 480");
        v.flipSpeed = 200;
        check(choice(Item::flipSpeed, v) == 3, "a launcher flip speed shows the step nearest to it");
        check(choice(Item::none, v) == 0 && !choose(Item::none, 0, v), "a heading holds no value");
        // RESET: each setting's default choice puts its default back.
        for (const auto& row : rows())
            choose(row.item, defaultChoice(row.item), v);
        check(v == defaults, "every default choice together is Spidy's defaults");
        // What the tab does not offer (run_game_vr.py's switches set it) stays as the session started.
        v.webGrab = v.punch = v.body = v.webShooter = false;
        for (const auto& row : rows())
            choose(row.item, defaultChoice(row.item), v);
        check(!v.webGrab && !v.punch && !v.body && !v.webShooter,
              "RESET ALL switched on web grab, punching, the body or the web shooter");
        const auto clean = sanitized({true, true, true, true, 90, 120, -5, 7});
        check(clean.swingSpeed == 65 && clean.snapTurn == 90 && clean.haptics == 0 && clean.screenSize == 2,
              "values outside the ranges");
        Values spun;
        spun.smoothTurn = 999;
        check(sanitized(spun).smoothTurn == 360, "smooth turning past a turn a second");
        Values heavy, light;
        heavy.weight = 999;
        light.weight = 0;
        check(sanitized(heavy).weight == 300 && sanitized(light).weight == 40, "weights past either end");
        Values big, none;
        big.hud = 9;
        none.hud = -1;
        check(sanitized(big).hud == 3 && sanitized(none).hud == 0, "HUD sizes past either end");
        Values fast, slow;
        fast.flipSpeed = 9999;
        slow.flipSpeed = 0;
        check(sanitized(fast).flipSpeed == 480 && sanitized(slow).flipSpeed == 90, "flip speeds past either end");
        check(gravity(300) <= game_swing::maxGravity, "the heaviest weight is more than a swing takes");
        near(screenWidth(0), 2.4f);
        near(screenWidth(7), 4.2f);
    });
    test("game rig snap turn takes the VR settings' angle, or none", [] {
        for (const float snap : {0.f, 1.5707963f}) {
            GameTrackingRig rig;
            rig.snapTurn(snap);
            rig.reset();
            auto f = trackedFrame();
            const auto before = rig.update(f, {}, {0, 0, -1}, true);
            f.predictedDisplayTime = 2;
            f.hands[1].stickX = 1;
            const auto after = rig.update(f, {}, {0, 0, -1}, true);
            near(after.head[8] - before.head[8], snap > 0 ? 1.f : 0.f);
        }
    });
    test("game rig smooth turn: steady while the stick is held, as fast as it is tilted, never a snap", [] {
        // 90 degrees a second at full tilt; a second of the stick at each tilt (negative: left).
        const std::pair<float, float> held[] = {{1.f, 90.f}, {.55f, 45.f}, {.15f, 0.f}, {-1.f, -90.f}};
        for (const auto& [tilt, degrees] : held) {
            GameTrackingRig rig;
            rig.smoothTurn(1.5707963f);
            rig.reset();
            auto f = trackedFrame();
            f.head.position.x = .4f;
            f.predictedDisplayTime = 1'000'000'000;
            const auto before = rig.update(f, {}, {0, 0, -1}, true);
            f.hands[1].stickX = tilt;
            GameMotionFrame m;
            for (int i = 0; i < 90; ++i) {
                f.predictedDisplayTime += 11'111'111;
                m = rig.update(f, {}, {0, 0, -1}, true);
            }
            // Turned about the head, which stays where it was.
            const float turned = degrees * 3.14159265f / 180;
            near(m.head[8], std::sin(turned), .002f);
            near(m.head[10], -std::cos(turned), .002f);
            for (int i = 12; i < 15; ++i)
                near(m.head[i], before.head[i]);
        }
    });
    test("game rig smooth turn goes on through a stutter; a stick held out of a break waits for release", [] {
        constexpr std::int64_t ms = 1'000'000;
        GameTrackingRig rig;
        rig.smoothTurn(1.5707963f);
        auto f = trackedFrame();
        f.seconds = .01f;
        f.predictedDisplayTime = 1000 * ms;
        rig.update(f, {}, {0, 0, -1}, true);
        const auto next = [&](bool gameplay, std::int64_t after = 10) {
            f.predictedDisplayTime += after * ms;
            return rig.update(f, {}, {0, 0, -1}, gameplay);
        };
        f.hands[1].stickX = 1;
        const auto turning = next(true);
        check(turning.head[8] > .01f, "the held stick did not turn");
        // A game frame over 100 ms closes the gate for a moment; the stick stays held.
        next(false);
        const auto stutter = next(true);
        check(stutter.head[8] > turning.head[8] + .01f, "a stutter stopped the turn");
        // Back from a menu with the stick still held: no turn until it is let go.
        next(false, 600);
        const auto back = next(true);
        const auto held = next(true);
        near(back.head[8], stutter.head[8]);
        near(held.head[8], stutter.head[8]);
        f.hands[1].stickX = 0;
        next(true);
        f.hands[1].stickX = 1;
        check(next(true).head[8] > held.head[8] + .01f, "the stick let go and held again did not turn");
    });
    test("momentum mode supports manual left-stick rotation and an airborne A brake", [] {
        GameTrackingRig rig;
        RealisticFlipSettings settings; settings.enabled = true;
        rig.automaticFlips(settings); rig.flips(true); rig.flipSpeed(3.14159265f);
        rig.flight({{}, {}, true, false});
        auto f = trackedFrame();
        flipFrame(rig, f, false, false);
        flipFrame(rig, f, false, true);
        f.hands[0].stickY = 1;
        GameMotionFrame m;
        for (int i = 0; i < 45; ++i) m = flipFrame(rig, f, false, true);
        check(std::abs(m.flipTilt.x) > .1f, "left stick must rotate with momentum mode on");
        check(length(m.swing.move) < .001f, "stick rotation must not also steer flight movement");
        const auto beforeBrake = m.flipTilt;
        m = flipFrame(rig, f, true, true);
        check(std::abs(beforeBrake.x * m.flipTilt.x + beforeBrake.y * m.flipTilt.y +
                       beforeBrake.z * m.flipTilt.z + beforeBrake.w * m.flipTilt.w) > .99f,
              "A recovery must not snap");
        f.hands[0].stickY = 0;
        for (int i = 0; i < 240; ++i) m = flipFrame(rig, f, false, true);
        near(length(m.flipTilt.rotate({0, 1, 0}) - Vec3{0, 1, 0}), 0, .001f);
    });
    test("physical arm gestures generate airborne rotation without a button or tether", [] {
        GameTrackingRig rig;
        RealisticFlipSettings settings; settings.enabled = true;
        rig.automaticFlips(settings);
        rig.flight({{}, {}, true, false});
        auto frame = trackedFrame();
        flipFrame(rig, frame, false, false);
        GameMotionFrame moved;
        for (int i = 0; i < 12; ++i) {
            for (auto& hand : frame.hands) hand.grip.position.y += .035f;
            moved = flipFrame(rig, frame, false, true);
        }
        check(moved.active && std::abs(moved.flipTilt.x) > .001f,
              "raw upward hand movement must turn the view without A, stick or web");
        const auto first = moved.flipTilt;
        for (int i = 0; i < 60; ++i) moved = flipFrame(rig, frame, false, true);
        check(std::abs(first.x - moved.flipTilt.x) > .05f, "rotation must continue after arms stop");
    });
    test("automatic rotation turns eyes and hands about the head without stick input", [] {
        GameTrackingRig rig;
        RealisticFlipSettings settings; settings.enabled = true; settings.strength = 100;
        rig.automaticFlips(settings);
        rig.flight({{0, 0, -15}, {1, 0, 0}, true, true});
        auto f = trackedFrame();
        f.hands[0].stickX = f.hands[0].stickY = 0;
        GameMotionFrame m;
        for (int i = 0; i < 60; ++i) m = flipFrame(rig, f, false);
        check(m.active && !levelTilt(m.flipTilt), "automatic rotation needs no A or stick");
        check(std::abs(m.headPose.orientation.z) > .1f, "head must rotate with the body");
        const Vec3 hand = m.grips[0].position - m.headPose.position;
        near(length(hand), length(f.hands[0].grip.position - f.head.position), .001f);
    });
    test("flip: a tap of A in the air is one front flip about the head, and comes out level", [] {
        GameTrackingRig rig;
        rig.flips(true);
        auto f = trackedFrame();
        const auto start = flipFrame(rig, f, false);
        const Vec3 head = matrixRow(start.head, 3), hand = start.hands[1].position;
        auto m = flipFrame(rig, f, true);
        check(levelTilt(m.swing.tilt), "A pressed alone turned the player");
        m = flipFrame(rig, f, false);
        check(!levelTilt(m.swing.tilt), "a tap did not start a flip");
        bool down = false, upsideDown = false;
        int frames = 1;
        for (; frames < 300 && !levelTilt(m.swing.tilt); ++frames) {
            m = flipFrame(rig, f, false);
            // The eyes stay where they are; the hands turn about the head.
            check(length(matrixRow(m.head, 3) - head) < 1e-4f, "the head moved in the flip");
            near(length(m.hands[1].position - head), length(hand - head), 1e-4f);
            const Vec3 ahead = matrixRow(m.head, 2), up = matrixRow(m.head, 1) * -1.f;
            if (frames < 25)
                down = down || ahead.y < -.5f;
            upsideDown = upsideDown || up.y < -.95f;
        }
        check(down, "a front flip did not look down first");
        check(upsideDown, "the flip never turned the player upside down");
        check(frames >= 95 && frames <= 115, ("one flip took " + std::to_string(frames) + " frames").c_str());
        for (int i = 0; i < 16; ++i)
            near(m.head[i], start.head[i], 1e-5f);
        check(levelTilt(flipFrame(rig, f, false).swing.tilt), "the flip did not stay level");
    });
    test("flip: A pressed on the ground is a jump, held into the air no flip; held in the air the stick turns", [] {
        GameTrackingRig rig;
        rig.flips(true);
        auto f = trackedFrame();
        f.hands[0].stickY = 1;
        flipFrame(rig, f, false, false);
        auto m = flipFrame(rig, f, true, false);
        for (int i = 0; i < 30; ++i) {
            check(levelTilt(m.swing.tilt), "a jump from the ground flipped");
            m = flipFrame(rig, f, true, true);
        }
        check(length(m.swing.move) > .5f, "a jump held into the air took the stick");
        flipFrame(rig, f, false, true);
        for (int i = 0; i < 6; ++i)
            m = flipFrame(rig, f, true, true);
        check(!levelTilt(m.swing.tilt), "A held in the air with the stick did not turn the player");
    });
    test("flip: held, the stick turns as fast as it is tilted and at rest holds the angle; let go, level", [] {
        constexpr float turn = 6.2831853f, full = turn / FlipMotion::holdTurnSeconds;
        FlipMotion::Sample s;
        s.airborne = true;
        s.seconds = 1.f / 90;
        const auto run = [&](FlipMotion& flip, int frames) {
            for (int i = 0; i < frames; ++i)
                flip.update(s);
        };
        const auto toLevel = [&](FlipMotion& flip) {
            int frames = 0;
            for (; frames < 1000 && !flip.level(); ++frames)
                flip.update(s);
            check(flip.level() && levelTilt(flip.tilt()), "the player did not come out level");
            return frames;
        };
        // A held alone turns nothing, and let go after a tap's time it is no flip.
        FlipMotion still;
        s.jump = true;
        run(still, 60);
        check(levelTilt(still.tilt()) && still.turned() == 0, "A held alone turned the player");
        s.jump = false;
        run(still, 1);
        check(still.level() && levelTilt(still.tilt()), "a long press alone flipped");
        // The stick full ahead: a whole turn in holdTurnSeconds, once up to speed;
        // tilted half way past the dead zone, half as fast.
        FlipMotion held;
        s.jump = true;
        s.stickY = 1;
        run(held, 45);
        float before = held.turned();
        run(held, 45);
        near(held.turned() - before, full * .5f, .02f);
        s.stickY = FlipMotion::stickDeadZone + (FlipMotion::stickFull - FlipMotion::stickDeadZone) / 2;
        run(held, 30);
        before = held.turned();
        run(held, 45);
        near(held.turned() - before, full * .25f, .02f);
        // At rest the stick keeps the player where they are.
        s.stickY = 0;
        run(held, 30);
        const Quat kept = held.tilt();
        run(held, 30);
        const Quat now = held.tilt();
        near(std::abs(kept.x * now.x + kept.y * now.y + kept.z * now.z + kept.w * now.w), 1, 1e-5f);
        // Let go: back level the short way.
        before = held.turned();
        s.jump = false;
        toLevel(held);
        check(held.turned() - before <= turn / 2 + .01f, "letting go went the long way round");
        // A tap with the stick back: a whole backflip, the turn the stick began carried on.
        FlipMotion back;
        s.stickY = -1;
        s.jump = true;
        run(back, 10);
        check(back.tilt().x > 0, "the stick back did not turn a backflip");
        s.jump = false;
        toLevel(back);
        near(back.turned(), turn, .01f);
        // Tapped again during a flip, the flip carries on to level: one whole turn.
        FlipMotion again;
        s.stickY = 0;
        s.jump = true;
        run(again, 1);
        s.jump = false;
        run(again, 30);
        check(!again.level(), "the tap's flip did not start");
        s.jump = true;
        run(again, 2);
        s.jump = false;
        toLevel(again);
        near(again.turned(), turn, .01f);
    });
    test("flip: held, the left stick picks the way and moves the player no more; let go, it moves them again", [] {
        // Ahead a front flip, back a backflip, right a cartwheel to the right; at rest nothing.
        const struct {
            float x, y;
        } sticks[] = {{0, 0}, {0, 1}, {0, -1}, {1, 0}};
        for (const auto& stick : sticks) {
            GameTrackingRig rig;
            rig.flips(true);
            auto f = trackedFrame();
            f.hands[0].stickX = stick.x;
            f.hands[0].stickY = stick.y;
            const bool still = stick.x == 0 && stick.y == 0;
            const auto before = flipFrame(rig, f, false);
            check(still || length(before.swing.move) > .5f, "the stick did not walk");
            auto m = flipFrame(rig, f, true);
            for (int i = 0; i < 30; ++i) {
                check(length(m.swing.move) == 0 && m.walkForward == 0 && m.walkRight == 0,
                      "the stick moved the player while A was held in the air");
                m = flipFrame(rig, f, true);
            }
            const Vec3 ahead = matrixRow(m.head, 2), up = matrixRow(m.head, 1) * -1.f;
            if (still)
                check(levelTilt(m.swing.tilt), "A held with the stick at rest turned the player");
            else if (stick.x > 0)
                check(up.x > .5f && std::abs(ahead.y) < .05f, "the stick to the right did not cartwheel right");
            else if (stick.y < 0)
                check(ahead.y > .5f, "the stick back did not backflip");
            else
                check(ahead.y < -.5f, "the stick ahead did not front flip");
            // Let go of A: the stick moves the player again while they turn back level.
            m = flipFrame(rig, f, false);
            check(still || !levelTilt(m.swing.tilt), "the player snapped level as A was let go");
            check(still || length(m.swing.move) > .5f, "the stick stayed the flip's");
            int frames = 0;
            for (; frames < 60 && !levelTilt(m.swing.tilt); ++frames)
                m = flipFrame(rig, f, false);
            check(levelTilt(m.swing.tilt) && frames <= 30, "letting go did not bring the player back level");
        }
    });
    test("flip: in the air the left stick alone turns the player once let go there; held from a jump it walks", [] {
        GameTrackingRig rig;
        rig.flips(true);
        auto f = trackedFrame();
        // A running jump: the stick held from the ground into the air moves the player.
        f.hands[0].stickY = 1;
        flipFrame(rig, f, false, false);
        auto m = flipFrame(rig, f, false, false);
        for (int i = 0; i < 30; ++i) {
            m = flipFrame(rig, f, false);
            check(levelTilt(m.swing.tilt) && length(m.swing.move) > .5f, "a stick held into the air flipped");
        }
        // Let go in the air, then pushed: ahead a front flip, back a backflip, the player moved no more.
        const float ways[] = {1, -1};
        for (const float way : ways) {
            f.hands[0].stickY = 0;
            flipFrame(rig, f, false);
            f.hands[0].stickY = way;
            for (int i = 0; i < 30; ++i) {
                m = flipFrame(rig, f, false);
                check(length(m.swing.move) == 0 && m.walkForward == 0 && m.walkRight == 0,
                      "the stick moved the player while it flipped them");
            }
            const Vec3 ahead = matrixRow(m.head, 2);
            check(way > 0 ? ahead.y < -.5f : ahead.y > .5f,
                  way > 0 ? "the stick ahead did not front flip" : "the stick back did not backflip");
            // Let go: back level the short way, no snap.
            f.hands[0].stickY = 0;
            m = flipFrame(rig, f, false);
            check(!levelTilt(m.swing.tilt), "the player snapped level as the stick was let go");
            int frames = 0;
            for (; frames < 60 && !levelTilt(m.swing.tilt); ++frames)
                m = flipFrame(rig, f, false);
            check(levelTilt(m.swing.tilt) && frames <= 30, "letting go of the stick did not bring the player level");
        }
        // Landing mid-flip levels the player; from the ground the stick walks again, into the air too.
        f.hands[0].stickY = 1;
        for (int i = 0; i < 30; ++i)
            m = flipFrame(rig, f, false);
        check(!levelTilt(m.swing.tilt), "no flip before the landing");
        int frames = 0;
        for (; frames < 60 && !levelTilt(m.swing.tilt); ++frames)
            m = flipFrame(rig, f, false, false);
        check(levelTilt(m.swing.tilt) && frames <= 40, "landing did not level the player quickly");
        for (int i = 0; i < 20; ++i) {
            m = flipFrame(rig, f, false, i >= 10);
            check(levelTilt(m.swing.tilt) && length(m.swing.move) > .5f, "the stick held from the landing flipped");
        }
        // On a wall (in the air as the game has it there) the stick crawls: no flip.
        const auto wallFrame = [&] {
            ++f.predictedDisplayTime;
            return rig.update(f, {}, {0, 0, -1}, true, {1, 0, 0}, true);
        };
        f.hands[0].stickY = 0;
        for (int i = 0; i < 20; ++i)
            wallFrame();
        f.hands[0].stickY = 1;
        for (int i = 0; i < 30; ++i) {
            m = wallFrame();
            check(levelTilt(m.swing.tilt) && length(m.swing.move) > .5f, "the stick flipped the player on a wall");
        }
        // Flips off: the stick let go and pushed in the air walks, level.
        GameTrackingRig off;
        f.hands[0].stickY = 0;
        flipFrame(off, f, false);
        f.hands[0].stickY = 1;
        for (int i = 0; i < 20; ++i) {
            m = flipFrame(off, f, false);
            check(levelTilt(m.swing.tilt) && length(m.swing.move) > .5f, "flips off, the stick in the air flipped");
        }
    });
    test("flip: the stick alone turns as fast as it is tilted; A held keeps the angle; a flick is no whole flip", [] {
        constexpr float turn = 6.2831853f, full = turn / FlipMotion::holdTurnSeconds;
        FlipMotion::Sample s;
        s.airborne = true;
        s.seconds = 1.f / 90;
        const auto run = [&](FlipMotion& flip, int frames) {
            for (int i = 0; i < frames; ++i)
                flip.update(s);
        };
        const auto toLevel = [&](FlipMotion& flip) {
            int frames = 0;
            for (; frames < 1000 && !flip.level(); ++frames)
                flip.update(s);
            check(flip.level() && levelTilt(flip.tilt()), "the player did not come out level");
        };
        FlipMotion stick;
        run(stick, 1);
        s.stickY = 1;
        run(stick, 45);
        check(stick.steering(), "the stick alone did not steer the flip");
        float before = stick.turned();
        run(stick, 45);
        near(stick.turned() - before, full * .5f, .02f);
        // A pressed while it turns, then the stick at rest: the angle stays while A is held.
        s.jump = true;
        run(stick, 5);
        s.stickY = 0;
        run(stick, 30);
        const Quat kept = stick.tilt();
        run(stick, 30);
        const Quat now = stick.tilt();
        near(std::abs(kept.x * now.x + kept.y * now.y + kept.z * now.z + kept.w * now.w), 1, 1e-5f);
        // Let go of A: back level the short way.
        before = stick.turned();
        s.jump = false;
        toLevel(stick);
        check(stick.turned() - before <= turn / 2 + .01f, "letting go went the long way round");
        // A flick of the stick: back level the way it came, no whole turn.
        FlipMotion flick;
        run(flick, 1);
        s.stickY = 1;
        run(flick, 6);
        s.stickY = 0;
        toLevel(flick);
        check(flick.turned() < turn / 4, "a flick of the stick flipped the player round");
        // A tap of A while the stick turns the player is no whole flip either.
        FlipMotion tapped;
        run(tapped, 1);
        s.stickY = 1;
        run(tapped, 20);
        s.jump = true;
        run(tapped, 3);
        s.jump = false;
        run(tapped, 10);
        s.stickY = 0;
        toLevel(tapped);
        check(tapped.turned() < turn / 2, "a tap during the stick's turn flipped the player round");
    });
    test("flip speed: the stick's turn, a tap's flip and the way back all go as fast as FLIP SPEED says", [] {
        constexpr float turn = 6.2831853f, degree = turn / 360;
        FlipMotion::Sample s;
        s.airborne = true;
        s.seconds = 1.f / 90;
        const auto run = [&](FlipMotion& flip, int frames) {
            for (int i = 0; i < frames; ++i)
                flip.update(s);
        };
        const auto toLevel = [&](FlipMotion& flip) {
            int frames = 0;
            for (; frames < 1000 && !flip.level(); ++frames)
                flip.update(s);
            check(flip.level() && levelTilt(flip.tilt()), "the player did not come out level");
            return frames;
        };
        // The stick at full tilt: 180 degrees a second, once up to speed.
        FlipMotion slow;
        slow.speed(180 * degree);
        run(slow, 1);
        s.stickY = 1;
        run(slow, 45);
        float before = slow.turned();
        run(slow, 45);
        near(slow.turned() - before, turn / 4, .02f);
        // Let go: back level as much slower as the flip, against the first flips' 240 degrees a second.
        FlipMotion first;
        s.stickY = 0;
        run(first, 1);
        s.stickY = 1;
        run(first, 69);
        check(std::abs(first.turned() - slow.turned()) < .05f, "the two flips did not turn alike before letting go");
        s.stickY = 0;
        const float ratio = static_cast<float>(toLevel(slow)) / static_cast<float>(toLevel(first));
        check(ratio > 1.25f && ratio < 1.42f, ("the way back took " + std::to_string(ratio) + " times as long").c_str());
        // A tap's whole flip: a third longer at 180 than at 240.
        const auto tapFrames = [&](FlipMotion& flip) {
            s.jump = true;
            run(flip, 1);
            s.jump = false;
            return toLevel(flip);
        };
        FlipMotion tapSlow, tapFirst;
        tapSlow.speed(180 * degree);
        const float tapRatio = static_cast<float>(tapFrames(tapSlow)) / static_cast<float>(tapFrames(tapFirst));
        check(tapRatio > 1.25f && tapRatio < 1.42f, ("a tap took " + std::to_string(tapRatio) + " times as long").c_str());
        near(tapSlow.turned(), turn, .01f);
        // A reset keeps the speed; past either end it is a quarter or four times the first flips'.
        slow.reset();
        s.stickY = 0;
        run(slow, 1);
        s.stickY = 1;
        run(slow, 45);
        before = slow.turned();
        run(slow, 45);
        near(slow.turned() - before, turn / 4, .02f);
        FlipMotion fastest, odd;
        fastest.speed(1e6f);
        odd.speed(std::numeric_limits<float>::quiet_NaN());
        s.stickY = 0;
        run(fastest, 1);
        run(odd, 1);
        s.stickY = 1;
        run(fastest, 45);
        run(odd, 45);
        before = fastest.turned();
        const float oddBefore = odd.turned();
        run(fastest, 9);
        run(odd, 9);
        near(fastest.turned() - before, 4 * turn / FlipMotion::holdTurnSeconds * .1f, .02f);
        near(odd.turned() - oddBefore, turn / FlipMotion::holdTurnSeconds * .1f, .01f);
        // The rig passes it on and keeps it through a reset: half the first flips' 240 turns half as far.
        const auto tiltAngle = [](Quat q) { return 2 * std::acos(std::min(1.f, std::abs(q.w))); };
        float angles[2]{};
        for (int k = 0; k < 2; ++k) {
            GameTrackingRig rig;
            rig.flips(true);
            if (k)
                rig.flipSpeed(120 * degree);
            rig.reset();
            auto f = trackedFrame();
            flipFrame(rig, f, false);
            f.hands[0].stickY = 1;
            GameMotionFrame m;
            for (int i = 0; i < 40; ++i)
                m = flipFrame(rig, f, false);
            angles[k] = tiltAngle(m.swing.tilt);
        }
        check(angles[0] > .5f && std::abs(angles[1] / angles[0] - .5f) < .03f,
              "the rig's flip speed did not halve the turn");
    });
    test("flip: landing brings it back level quickly; a menu at once; a held A turns no more", [] {
        GameTrackingRig rig;
        rig.flips(true);
        auto f = trackedFrame();
        f.hands[0].stickY = 1;
        flipFrame(rig, f, false);
        auto m = flipFrame(rig, f, true);
        for (int i = 0; i < 75; ++i)
            m = flipFrame(rig, f, true);
        check(matrixRow(m.head, 1).y > .5f, "the held flip was not upside down");
        int frames = 0;
        for (; frames < 60 && !levelTilt(m.swing.tilt); ++frames)
            m = flipFrame(rig, f, true, false);
        check(levelTilt(m.swing.tilt) && frames <= 40, "landing did not level the player quickly");
        for (int i = 0; i < 20; ++i)
            check(levelTilt(flipFrame(rig, f, true, true).swing.tilt), "A held from the landing turned again");
        // Mid-flip a menu opens: level when play comes back, A still held.
        flipFrame(rig, f, false);
        m = flipFrame(rig, f, true);
        for (int i = 0; i < 20; ++i)
            m = flipFrame(rig, f, true);
        check(!levelTilt(m.swing.tilt), "no flip before the menu");
        ++f.predictedDisplayTime;
        check(!rig.update(f, {}, {0, 0, -1}, false, {0, 1, 0}, true).active, "the menu kept play going");
        for (int i = 0; i < 5; ++i)
            check(levelTilt(flipFrame(rig, f, true).swing.tilt), "the player came back from a menu tilted");
    });
    test("flip: off unless FLIPS is on, A in the air does nothing; switched off mid-flip, level at once", [] {
        GameTrackingRig rig;
        auto f = trackedFrame();
        f.hands[0].stickY = 1;
        flipFrame(rig, f, false);
        // Off (the default): A held, then let go, in the air turns nothing; the stick still walks.
        for (int i = 0; i < 40; ++i) {
            const auto m = flipFrame(rig, f, i < 30);
            check(levelTilt(m.swing.tilt) && length(m.swing.move) > .5f, "flips off, A in the air flipped");
        }
        // On, A held with the stick turns the player; switched off, they are level at once.
        rig.flips(true);
        auto m = flipFrame(rig, f, true);
        for (int i = 0; i < 20; ++i)
            m = flipFrame(rig, f, true);
        check(!levelTilt(m.swing.tilt), "flips on, A held with the stick did not turn");
        rig.flips(false);
        m = flipFrame(rig, f, true);
        check(levelTilt(m.swing.tilt) && length(m.swing.move) > .5f, "switched off mid-flip, the player stayed tilted");
        // A reset (the flat screen, another player) keeps the switch.
        rig.flips(true);
        rig.reset();
        flipFrame(rig, f, false);
        m = flipFrame(rig, f, true);
        for (int i = 0; i < 6; ++i)
            m = flipFrame(rig, f, true);
        check(!levelTilt(m.swing.tilt), "a reset switched the flips off");
    });
    test("rig: a flip's tilt turns the tracking space about its pivot; level it is the yawed rig, exactly", [] {
        const Rig level{{10, 20, 30}, .5f};
        Rig tilted = level;
        tilted.pivot = {.1f, 1.7f, -.2f};
        tilted.tilt = Quat::around({1, 0, .4f}, 2.f);
        const Pose p{{.3f, 1.2f, -.5f}, Quat::around({0, 1, 0}, .3f)};
        const Vec3 pivot = tilted.toWorld({tilted.pivot, {}}).position;
        near(length(pivot - level.toWorld({tilted.pivot, {}}).position), 0);
        near(length(tilted.toWorld(p).position - pivot), length(p.position - tilted.pivot));
        const Quat q = tilted.toWorld(p).orientation, want = tilted.orientation() * p.orientation;
        near(std::abs(q.x * want.x + q.y * want.y + q.z * want.z + q.w * want.w), 1, 1e-5f);
        // A snap turn keeps the head where it is, tilted too.
        const Vec3 h{.1f, 1.6f, .2f};
        const auto before = tilted.toWorld({h, {}}).position;
        tilted.turn(.5235988f, h);
        near(length(tilted.toWorld({h, {}}).position - before), 0);
        // Level, the pivot changes nothing at all.
        Rig moved = level;
        moved.pivot = {5, 5, 5};
        const auto a = moved.toWorld(p), b = level.toWorld(p);
        check(a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z,
              "a level rig moved with its pivot");
    });
    test("flip: upside down, a hand pulled away from the anchor zips, one pushed toward it does not", [] {
        // The anchor overhead is at the upside-down player's feet: up in the room is away from it.
        for (const bool away : {true, false}) {
            TestWorld w;
            spidy::Swing s(inert());
            auto in = aimed();
            in.tilt = Quat::around({1, 0, 0}, 3.14159265f);
            s.update(.01f, in, w);
            int zips = 0;
            for (int i = 0; i < 20; ++i) {
                in.hands[0].gripRelativeToHead.y += away ? .03f : -.03f;
                s.update(.01f, in, w);
                for (auto e : s.events())
                    zips += e.kind == EventKind::Zip;
            }
            check(zips == (away ? 1 : 0), away ? "a pull upside down did not zip" : "a push upside down zipped");
        }
    });
    test("punch: a flip turning the player is no motion of the arm", [] {
        // A fist held out while a flip turns the player a whole turn in 0.7 s: over 5 m/s in the world.
        const Vec3 head{0, 1.6f, 0}, arm{0, -.2f, -.6f}, axis{-1, 0, .3f};
        const float step = 8.975979f / 90;
        const auto toWorld = [&](int i) { return Quat::yaw(.4f) * Quat::around(axis, step * static_cast<float>(i)); };
        Punches turned, unturned;
        std::vector<PunchEvent> out;
        const std::vector<PunchTarget> none;
        for (int i = 0; i < 30; ++i) {
            const Quat q = toWorld(i);
            std::array<PunchHand, 2> hands{};
            hands[1] = {true, head + q.rotate(arm), q.rotate(arm), false};
            if (i)
                turned.turn(q * toWorld(i - 1).conjugate());
            turned.update(1.f / 90, hands, none, out);
            unturned.update(1.f / 90, hands, none, out);
        }
        check(unturned.speed(1) > unturned.config().minSpeed, "the test's flip is too slow to look like a punch");
        check(turned.speed(1) < .01f, "the flip counted as the arm's motion");
    });
    test("aim marker motion: a flip turning the player is no motion of the aim", [] {
        constexpr float dt = 1.f / 72;
        const Vec3 held = normalized(Vec3{.1f, -.2f, -1}); // the hand still, in the tracking space
        const auto angle = [](Vec3 a, Vec3 b) { return std::atan2(length(cross(a, b)), dot(a, b)); };
        AimMarkerMotion tilted, yawOnly;
        float worst = 0, lagging = 0;
        for (int i = 0; i < 40; ++i) {
            const int64_t t = 1'000'000'000 + static_cast<int64_t>(i * dt * 1e9);
            const Quat turn = Quat::yaw(.3f) * Quat::around({-1, 0, 0}, 8.975979f * dt * static_cast<float>(i));
            const Vec3 world = turn.rotate(held);
            tilted.begin(t);
            yawOnly.begin(t);
            worst = std::max(worst, angle(tilted.aim(world, turn), world));
            lagging = std::max(lagging, angle(yawOnly.aim(world, .3f), world));
        }
        check(worst < 1e-4f, "the flip swung the steadied aim");
        check(lagging > .004f, "the test's flip is too slow to show in a yaw-only steadying");
    });
    test("game swing command: version 2 (the probes') is level, version 3 carries a flip's tilt", [] {
        game_swing::Command c;
        c.focused = 1;
        c.serial = 1;
        check(c.version == 4 && c.bytes == 200 && game_swing::valid(c), "a level version 4 command was rejected");
        c.version = 3;
        c.bytes = game_swing::commandBytesV3;
        check(game_swing::valid(c), "a level version 3 command was rejected");
        c.tilt = Quat::around({1, 0, .2f}, 2.f);
        check(game_swing::valid(c), "a flip's tilt was rejected");
        const auto tilt = game_swing::input(c).tilt;
        near(tilt.x, c.tilt.x, 1e-6f);
        near(tilt.z, c.tilt.z, 1e-6f);
        near(tilt.w, c.tilt.w, 1e-6f);
        auto v2 = c;
        v2.version = 2;
        v2.bytes = game_swing::commandBytesV2;
        v2.tilt = {};
        check(game_swing::valid(v2) && levelTilt(game_swing::input(v2).tilt), "a probe's version 2 command was not level");
        auto bad = c;
        bad.tilt = {0, 0, 0, 2};
        check(!game_swing::valid(bad), "a scaled tilt was accepted");
        bad = c;
        bad.version = 2;
        check(!game_swing::valid(bad), "a version 2 command of version 3's size was accepted");
    });
    test("body: a flip turns the whole body with the player about the eyes", [] {
        for (const bool odd : {false, true}) {
            auto b = testBody(odd);
            auto t = lookingAhead({0, 1.7f, .08f});
            t.airborne = true;
            t.hands[0] = {true, {.3f, 1.2f, .45f}, gripFacing({0, 0, 1})};
            t.hands[1] = {true, {-.25f, 1.55f, .4f}, gripFacing({0, .5f, .866f})};
            body::Config c;
            c.hideHead = false;
            body::State s;
            const auto level = solved(b, t, s, c);
            // The same player well into a front flip: everything turned about the eyes.
            const Quat tilt = body::axisAngle({1, 0, 0}, 2.2f);
            auto flipped = t;
            flipped.tilt = tilt;
            flipped.facing = tilt * t.facing;
            for (auto& h : flipped.hands) {
                h.grip = t.eyes + tilt.rotate(h.grip - t.eyes);
                h.orientation = tilt * h.orientation;
            }
            body::State s2;
            const auto turned = solved(b, flipped, s2, c);
            for (const int j : b.rig.below[static_cast<size_t>(b.rig.pelvis)])
                check(length(b.at(turned, j) - (t.eyes + tilt.rotate(b.at(level, j) - t.eyes))) < 2e-3f,
                      "the body did not turn with the player");
            for (int i = 0; i < 2; ++i)
                check(length(b.at(turned, b.rig.arms[i].hand) - wristOf(flipped.hands[i], i)) < 2e-3f,
                      "a wrist missed its controller in the flip");
        }
    });
    test("slow motion eases the game's time in and out along a smooth curve", [] {
        SlowMotion slow;
        const auto& t = slow.tuning();
        check(slow.timeScale() == 1 && slow.blend() == 0 && !slow.active(), "a new session starts slowed");
        check(slow.update(.011f, true, true) == SlowMotion::Event::started && slow.active(), "a press did not start it");
        float before = slow.timeScale(), steepest = 0;
        for (float s = .011f; s < t.enterSeconds + .05f; s += .011f) {
            check(slow.update(.011f, false, true) == SlowMotion::Event::none, "easing in raised an event");
            const float now = slow.timeScale();
            check(now <= before + 1e-6f, "time sped up while slowing");
            steepest = std::max(steepest, before - now);
            before = now;
        }
        near(slow.timeScale(), t.scale, 1e-4f);
        near(slow.blend(), 1);
        // No jolt: no frame changes time by more than a seventh of the whole way.
        check(steepest < (1 - t.scale) / 7, "the ease has a jolt");
        // Halfway along the curve time runs at the geometric mean (even steps in log time).
        SlowMotion half;
        half.update(.01f, true, true);
        float halfway = 1;
        for (float s = .01f; s < 2; s += .001f) {
            half.update(.001f, false, true);
            if (half.blend() >= .5f) {
                halfway = half.timeScale();
                break;
            }
        }
        near(halfway, std::sqrt(t.scale), .01f);
        check(slow.update(.011f, true, true) == SlowMotion::Event::stopped && !slow.active(), "a press did not end it");
        before = slow.timeScale();
        float seconds = 0;
        while (slow.timeScale() < 1 && seconds < 2) {
            slow.update(.011f, false, true);
            seconds += .011f;
            check(slow.timeScale() >= before - 1e-6f, "time slowed while easing out");
            before = slow.timeScale();
        }
        check(slow.timeScale() == 1 && std::abs(seconds - t.exitSeconds) < .03f, "easing out took the wrong time");
    });
    test("slow motion spends focus in real time and ends when it runs out", [] {
        SlowMotion slow;
        const auto& t = slow.tuning();
        slow.update(.01f, true, true);
        float seconds = .01f;
        SlowMotion::Event last{};
        while (slow.active() && seconds < 30) {
            last = slow.update(.01f, false, true);
            seconds += .01f;
        }
        check(last == SlowMotion::Event::emptied && slow.focus() == 0, "an empty meter did not end slow motion");
        near(seconds, t.drainSeconds, .03f);
        check(slow.view().warning > .9f, "running empty did not flash");
        check(slow.update(.01f, true, true) == SlowMotion::Event::refused && !slow.active(),
              "an empty meter started slow motion");
        // Refilling waits, then takes refillSeconds from empty.
        for (float s = 0; s < t.refillDelay - .05f; s += .01f)
            slow.update(.01f, false, true);
        check(slow.focus() == 0, "focus refilled before its delay");
        seconds = 0;
        while (slow.focus() < 1 && seconds < 60) {
            slow.update(.01f, false, true);
            seconds += .01f;
        }
        near(seconds, t.refillSeconds + .05f, .06f);
        // A press needs the minimum; a part-filled meter starts it above that.
        SlowMotion low;
        low.update(.01f, true, true);
        while (low.focus() > low.tuning().minimum * .5f)
            low.update(.01f, false, true);
        low.update(.01f, true, true);
        check(!low.active(), "the second press did not stop it");
        check(low.update(.01f, true, true) == SlowMotion::Event::refused, "a press below the minimum started it");
        while (low.focus() < low.tuning().minimum + .01f)
            low.update(.01f, false, true);
        check(low.update(.01f, true, true) == SlowMotion::Event::started, "a press over the minimum was refused");
    });
    test("slow motion ends with play and ignores presses outside it", [] {
        SlowMotion slow;
        check(slow.update(.01f, true, false) == SlowMotion::Event::none && !slow.active(),
              "a press on the game screen started it");
        slow.update(.01f, true, true);
        for (int i = 0; i < 50; ++i)
            slow.update(.01f, false, true);
        check(slow.update(.01f, false, false) == SlowMotion::Event::interrupted && !slow.active(),
              "a menu kept slow motion on");
        for (int i = 0; i < 100; ++i)
            slow.update(.01f, false, false);
        check(slow.timeScale() == 1, "time stayed slowed in a menu");
        // A hitch counts as a tenth of a second at most.
        SlowMotion hitch;
        hitch.update(.01f, true, true);
        const float focus = hitch.focus();
        hitch.update(5, false, true);
        near(focus - hitch.focus(), .1f / hitch.tuning().drainSeconds, 1e-4f);
        hitch.update(std::numeric_limits<float>::quiet_NaN(), false, true);
        check(std::isfinite(hitch.timeScale()) && std::isfinite(hitch.focus()), "a bad frame time broke it");
    });
    test("slow motion's view: a ring at each change, a meter while focus is spent", [] {
        SlowMotion slow;
        check(slow.view().meter == 0 && slow.view().ripple < 0, "a full meter shows at rest");
        slow.update(.01f, true, true);
        auto v = slow.view();
        check(v.active && v.entering && v.ripple >= 0 && v.ripple < .1f, "no ring at the start");
        for (int i = 0; i < 20; ++i)
            slow.update(.01f, false, true);
        check(slow.view().meter > .9f, "the meter did not show while spending focus");
        for (int i = 0; i < 40; ++i)
            slow.update(.01f, false, true);
        check(slow.view().ripple < 0, "the ring outlasted its time");
        slow.update(.01f, true, true);
        v = slow.view();
        check(!v.active && !v.entering && v.ripple >= 0, "no ring at the end");
        // It shows until the meter is full again and a moment after, then fades.
        float seconds = 0;
        while (slow.focus() < 1 && seconds < 30) {
            slow.update(.01f, false, true);
            seconds += .01f;
            check(slow.view().meter > .99f, "the meter faded while refilling");
        }
        for (float s = 0; s < SlowMotion::meterHold - .02f; s += .01f)
            slow.update(.01f, false, true);
        check(slow.view().meter > .99f, "the full meter faded at once");
        for (float s = 0; s < SlowMotion::meterOut + .05f; s += .01f)
            slow.update(.01f, false, true);
        check(slow.view().meter == 0, "the full meter stayed");
        // A refused press shows the meter, flashing red.
        SlowMotionTuning strict;
        strict.minimum = .9f;
        SlowMotion picky(strict);
        picky.update(.01f, true, true);
        while (picky.focus() > .85f)
            picky.update(.01f, false, true);
        picky.update(.01f, true, true);
        check(picky.update(.01f, true, true) == SlowMotion::Event::refused, "a press below the minimum started it");
        v = picky.view();
        check(v.warning > .9f && v.meter > 0 && !v.active, "a refused press did not flash the meter");
        // The minimum is a share of the meter: above 1, a full meter still suffices.
        strict.minimum = 1.5f;
        SlowMotion full(strict);
        check(full.update(.01f, true, true) == SlowMotion::Event::started, "a full meter was refused");
    });
    test("the left thumbstick's click is not half of clicking both", [] {
        StickPress press;
        // A short click counts when it is let go.
        check(!press.update(.011f, true, true, false), "a click counted while still short");
        check(!press.update(.011f, true, true, false), "a click counted while still short");
        check(press.update(.011f, true, false, false), "a short click did not count");
        check(!press.update(.011f, true, false, false), "a click counted twice");
        // A held click counts once, after the chord's time.
        int counted = 0;
        float at = -1;
        for (int i = 0; i < 40; ++i)
            if (press.update(.011f, true, true, false)) {
                ++counted;
                at = i * .011f;
            }
        check(counted == 1 && at >= StickPress::chordSeconds - .012f && at <= StickPress::chordSeconds + .012f,
              "a held click counted wrongly");
        check(!press.update(.011f, true, false, false), "letting go of a counted click counted again");
        // Both sticks, either first, never count.
        for (const bool rightFirst : {false, true}) {
            StickPress chord;
            bool any = chord.update(.011f, true, !rightFirst, rightFirst);
            for (int i = 0; i < 30; ++i)
                any = chord.update(.011f, true, true, true) || any;
            any = chord.update(.011f, true, false, false) || any;
            check(!any, "clicking both sticks counted as slow motion");
        }
        StickPress late;
        bool any = late.update(.011f, true, true, false);
        any = late.update(.05f, true, true, true) || any; // the right joins within the chord's time
        any = late.update(.011f, true, false, false) || any;
        check(!any, "a chord whose second stick came late counted");
        // Held through a menu, it waits for a release.
        StickPress menu;
        menu.update(.011f, false, true, false);
        for (int i = 0; i < 30; ++i)
            check(!menu.update(.011f, true, true, false), "a click held through a menu counted");
        check(!menu.update(.011f, true, false, false), "releasing a click held through a menu counted");
        check(!menu.update(.011f, true, true, false), "a new click counted at once");
        check(menu.update(.011f, true, false, false), "a new click after the menu did not count");
    });
#ifdef _WIN32
    test("the target scan knows a game class by what it derives from", [] {
        const auto base = reinterpret_cast<uintptr_t>(&__ImageBase);
        const auto classOf = [&](const std::unique_ptr<Component>& object) {
            uintptr_t vtable{};
            std::memcpy(&vtable, object.get(), sizeof(vtable));
            return game_targets::classOf(base, vtable);
        };
        const uint32_t bot = 1u << static_cast<unsigned>(game_targets::Kind::bot),
                       throwable = 1u << static_cast<unsigned>(game_targets::Kind::throwable);
        const std::unique_ptr<Component> docOck(new DocOckMoverManager), walker(new BotMoverManagerGame),
            boss(new SilverSable), civilian(new CivilianBot), bird(new BirdBot), prop(new ThrowableHelper),
            tracker(new StatusEffectTrackerWebbed), other(new Unrelated);
        // A mover manager two classes down from HoverMoverManager: a bot, and one that flies.
        const auto flying = classOf(docOck);
        check((flying & bot) && ((flying >> 8) & game_targets::hover),
              "a hover mover's subclass is no flying bot");
        const auto walking = classOf(walker);
        check((walking & bot) && !((walking >> 8) & game_targets::hover), "a walking bot was missed or flies");
        check(classOf(boss) >> 8 == game_targets::thug && !(classOf(boss) & bot),
              "a thug's subclass is no thug");
        check(classOf(civilian) >> 8 == game_targets::civilian, "a civilian was not one");
        check(classOf(bird) >> 8 == game_targets::neutral, "a bird was not neutral");
        check(classOf(prop) == throwable, "a throwable was not one");
        check(classOf(tracker) >> 8 == game_targets::webbable, "the webbing tracker was missed");
        // No class makes a device: a gun turret is one by the webbing tracker on its actor, not by its name.
        const std::unique_ptr<Component> turret(new EnemyTurret);
        check(!classOf(turret), "a turret's class was taken for something");
        check(!classOf(other), "an unrelated class was taken for something");
        static const uintptr_t plain[2]{};
        check(!game_targets::classOf(base, reinterpret_cast<uintptr_t>(&plain[1])),
              "data was taken for a class");
    });
    test("a thug waiting for his wave or knocked out is nobody to fight", [] {
        const auto base = reinterpret_cast<uintptr_t>(&__ImageBase);
        // A bot's state machine as the game lays it out: its state at +0x70, in use at +0x80; and his health
        // component, the hit points at +0x88.
        std::vector<uint8_t> machine(0x100), health(0x100);
        const auto put = [](std::vector<uint8_t>& in, size_t at, const auto& value) {
            std::memcpy(in.data() + at, &value, sizeof(value));
        };
        const auto state = [&](const Component* object) {
            put(machine, 0x70, reinterpret_cast<uintptr_t>(object));
            put(machine, 0x80, static_cast<uint8_t>(object != nullptr));
        };
        game_targets::Candidate thug;
        thug.kind = game_targets::Kind::bot;
        thug.traits = game_targets::thug | game_targets::webbable;
        thug.machine = reinterpret_cast<uint64_t>(machine.data());
        thug.health = reinterpret_cast<uint64_t>(health.data());
        put(health, 0x88, 70.f);
        const std::unique_ptr<Component> aiming(new BotStateAim), dying(new BotStateDeathAnimGame),
            limp(new BotStateRagdollGame), stuck(new BotStateWallStuck), fallen(new BotStateDeathAnim);
        // Waiting where his wave will appear: hit points and all, and no state.
        check(!game_targets::inPlay(thug) && !game_targets::up(base, thug) && !game_targets::fighting(base, thug),
              "a thug who has not appeared yet was someone to fight");
        put(machine, 0x80, static_cast<uint8_t>(1));
        check(!game_targets::inPlay(thug), "a state machine in use without a state has its bot in play");
        // In the fight.
        state(aiming.get());
        check(game_targets::inPlay(thug) && game_targets::up(base, thug) && game_targets::fighting(base, thug),
              "a thug who aims at the player was nobody to fight");
        // Knocked out: no hit points, and the game's own states for it, each by itself.
        put(health, 0x88, 0.f);
        check(game_targets::inPlay(thug) && !game_targets::up(base, thug) && !game_targets::fighting(base, thug),
              "a thug without hit points fights on");
        put(health, 0x88, 70.f);
        for (const auto* down : {dying.get(), limp.get(), stuck.get(), fallen.get()}) {
            state(down);
            check(game_targets::inPlay(thug) && !game_targets::up(base, thug) && !game_targets::fighting(base, thug),
                  "a thug who is down fights on");
        }
        // Without a health component, or without a state machine, a bot counts as in the fight.
        state(aiming.get());
        thug.health = 0;
        check(game_targets::fighting(base, thug), "a bot whose health cannot be read was nobody to fight");
        thug.machine = 0;
        check(game_targets::inPlay(thug) && game_targets::fighting(base, thug),
              "a bot without a state machine was nobody to fight");
        // A civilian is up, to be hit by a fist that meets him, and nobody to fight.
        thug.traits = game_targets::civilian;
        check(game_targets::up(base, thug) && !game_targets::fighting(base, thug), "a civilian was someone to fight");
        // Something else moves him: up, but not free to fight - a thug in a car's seat, one a cinematic plays,
        // a hurt officer in another's synced animation. Each state by itself and by what the game derives from it.
        thug.traits = game_targets::thug | game_targets::webbable;
        thug.machine = reinterpret_cast<uint64_t>(machine.data());
        thug.health = reinterpret_cast<uint64_t>(health.data());
        put(health, 0x88, 70.f);
        const std::unique_ptr<Component> seated(new BotStateAttachToActor), scene(new BotStatePlayCinematicGame),
            nothing(new BotStateNull), guest(new BotStatePlaySyncedAnimGuestGame), carried(new BotStateCarried),
            slotted(new BotStateSlottedInteract), frozen(new BotStateFrozen),
            yanked(new BotStateRequestTakedownYank), host(new BotStatePlaySyncedAnimHost),
            flung(new BotStateFlung);
        for (const auto* moved : {seated.get(), scene.get(), nothing.get(), guest.get(), carried.get(), slotted.get(),
                                  frozen.get(), yanked.get()}) {
            state(moved);
            check(game_targets::up(base, thug) && game_targets::held(base, thug) &&
                      !game_targets::fighting(base, thug),
                  "a thug something else moves was free to fight");
        }
        // His own AI moving him: hosting a synced animation (a mugger), flung by a web, aiming.
        for (const auto* own : {host.get(), flung.get(), aiming.get()}) {
            state(own);
            check(!game_targets::held(base, thug) && game_targets::fighting(base, thug),
                  "a thug his own AI moves was held");
        }
        // Waiting for his wave, with no state: neither held nor in the fight.
        state(nullptr);
        check(!game_targets::held(base, thug) && !game_targets::fighting(base, thug), "a waiting thug was held");
        // A device (a gun turret) stands while it has hit points; one without a health component counts as
        // standing. It is no bot: nobody to hit or to fight, whatever its hit points.
        game_targets::Candidate gun;
        gun.kind = game_targets::Kind::device;
        gun.traits = game_targets::webbable;
        gun.health = reinterpret_cast<uint64_t>(health.data());
        put(health, 0x88, 3.f);
        check(game_targets::standing(gun) && !game_targets::up(base, gun) && !game_targets::fighting(base, gun),
              "a turret does not stand, or is a bot");
        put(health, 0x88, 0.f);
        check(!game_targets::standing(gun), "a turret without hit points stands");
        gun.health = 0;
        check(game_targets::standing(gun), "a turret whose health cannot be read does not stand");
        thug.traits = game_targets::thug;
        check(!game_targets::standing(thug), "a thug is a device");
    });
    test("a bot's size comes from his mover and carries a street thug's measures over", [] {
        // A mover manager as the game lays one out: its body's class at
        // +0xdb8, the spheres' centres and radius from +0xdc0, the scale at +0xdf8.
        constexpr uintptr_t base = 0x140000000;
        std::vector<uint8_t> manager(0xe00);
        const auto put = [&](size_t at, const auto& value) {
            std::memcpy(manager.data() + at, &value, sizeof(value));
        };
        const auto address = reinterpret_cast<uint64_t>(manager.data());
        put(0xdb8, base + game_targets::moverBodySize);
        put(0xdc0, std::array<float, 3>{.85f, 1.15f, .45f});
        put(0xdf8, 1.f);
        game_targets::Size thug{9, 9, 9};
        check(game_targets::sizeOf(base, address, thug), "a street thug's body was not read");
        near(thug.low, .4f);
        near(thug.high, 1.6f);
        near(thug.radius, .45f);
        // A street thug keeps the measures tuned on him.
        for (const float h : {0.f, .95f, 1.15f, 1.8f})
            near(thug.height(h), h);
        near(thug.width(.3f), .3f);
        // The upper sphere scales with the mover (1fbba90).
        put(0xdf8, 2.f);
        game_targets::Size tall;
        check(game_targets::sizeOf(base, address, tall), "a scaled body was not read");
        near(tall.high, 2.75f);
        // A drone's small capsule about its centre, a heavy's big one.
        const game_targets::Size drone{-.3f, .3f, .2f}, heavy{.6f, 3.f, .9f};
        near(drone.height(.4f), -.3f);
        near(drone.height(1.6f), .3f);
        near(drone.height(1.15f), .075f);
        near(drone.width(.45f), .2f);
        near(heavy.height(1.8f), 3.4f);
        near(heavy.width(.3f), .6f);
        // Another class at the body's place, or an implausible body: no size.
        game_targets::Size kept;
        put(0xdf8, 1.f);
        put(0xdc8, 0.f);
        check(!game_targets::sizeOf(base, address, kept) && kept.high == 1.6f,
              "a body without radius was read");
        put(0xdc8, .45f);
        put(0xdb8, base + 8);
        check(!game_targets::sizeOf(base, address, kept) && kept.radius == .45f,
              "another class was read as a body");
    });
#endif
    std::cout << total - failed << '/' << total << " tests passed\n";
    return failed ? 1 : 0;
}

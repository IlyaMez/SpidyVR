#include "spidy/lab_world.hpp"
#include "spidy/native_view.hpp"
#include "spidy/game_tracking.hpp"
#include "spidy/native_eye_frame.hpp"
#include "spidy/native_hud.hpp"
#include "spidy/native_eye_history.hpp"
#include "spidy/eye_job_table.hpp"
#include "spidy/eye_pair_state.hpp"
#include "spidy/native_rays.hpp"
#include "spidy/native_render_memory.hpp"
#include "spidy/game_pad.hpp"
#include "spidy/game_swing.hpp"
#include "spidy/native_movement.hpp"
#include "spidy/presentation_gate.hpp"
#include "spidy/eye_resolution.hpp"
#include "spidy/eye_snapshot.hpp"
#include "spidy/vr_settings.hpp"
#include "spidy/vr_shortcut.hpp"
#include "spidy/web_visual.hpp"
#include "spidy/web_grab.hpp"
#include "spidy/lab_props.hpp"
#include "spidy/body_ik.hpp"
#include "spidy/body_calibration.hpp"
#include "spidy/overlay_text.hpp"
#include "spidy/punch.hpp"
#include "spidy/shooter.hpp"
#include "spidy/slow_motion.hpp"
#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
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
    test("visible hand fingers agree with aim while wrist stays at grip position", [] {
        GameTrackingRig rig; auto f=trackedFrame();
        f.hands[0].grip.orientation={.70710678f,0,0,.70710678f};
        f.hands[0].aim.orientation=Quat::yaw(.4f);
        rig.update(f,{10,20,30},{1,0,0},true);
        f.predictedDisplayTime=2;
        const auto out=rig.update(f,{10,20,30},{1,0,0},true);
        near(length(out.hands[0].orientation.rotate({0,0,-1})-
                    out.swing.hands[0].aim.orientation.rotate({0,0,-1})),0);
        near(length(out.hands[0].position-out.swing.hands[0].aim.position),0);
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
        near(on.swing.hands[1].aim.position.x-on.hands[1].position.x,0);
        near(length(on.anchor-feet),0);
        // Off the wall, the head comes back over the feet.
        const auto off=playFor(rig,f,feet,{0,1,0},.6f);
        check(!off.onSurface,"standing again kept the wall");near(off.head[12],10,.002f);
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
        near(config.gravity,7.848f);
        near(s.predictNativeStep(.02f,none,w,flying).velocity.y,-7.848f*.02f);
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
        Swing swing(config);
        Body actual{{0, 0, 0}, {}, false};
        auto in = aimed();
        swing.predictNativeStep(.02f, in, world, actual);
        near(swing.webs()[0].length, 20);
        // Native movement/yank carried us ten metres toward the existing anchor.
        actual.position.y = 10;
        in.hands[0].trigger = 0;
        swing.predictNativeStep(.02f, in, world, actual);
        near(swing.webs()[0].length, 20);
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
                check(length(actual.position - world.anchor) <= swing.webs()[0].length + .01f,
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
            const Vec3 requested = limited(average, swing.config().maxSpeed);
            const Vec3 moving = controlled ? command : achieved; // command in flight this step
            flight.issued(step + 1000, requested,
                          limited(intent.velocity + requested - average, swing.config().maxSpeed));
            serial = step + 1000;
            command = requested;
            controlled = true;
            last = position;
            position += moving * dt;
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
        const Vec3 eye{m.eyes[0][12],m.eyes[0][13],m.eyes[0][14]}, wrist=webWrist(m.hands[0]);
        near(length((wrist+shared)-(eye+shared)-(wrist-eye)),0);
        near(length((wrist+shared)-(eye+late)-(wrist-eye)),.71f);
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
    test("external prediction relinquishes stale or unfocused input", [] {
        TestWorld world; Swing swing;
        auto in=aimed(); Body actual{{0,0,0},{},false};
        check(swing.predictNativeStep(.01f,in,world,actual).valid,"initial native step rejected");
        check(swing.webs()[0].attached,"no web to release");
        check(!swing.predictNativeStep(.051f,in,world,actual).valid,"stale native frame accepted");
        check(!swing.webs()[0].attached,"stale input held web");
        in.focused=false;
        check(!swing.predictNativeStep(.01f,in,world,actual).valid,"unfocused native frame accepted");
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
    test("both thumbsticks toggle once and require full release after focus loss", [] {
        VrShortcut chord;
        check(!chord.update(true,true,true),"held startup buttons toggled");
        check(!chord.update(true,false,false),"release toggled");
        check(!chord.update(true,true,false),"one stick toggled");
        check(chord.update(true,true,true),"both sticks failed to toggle");
        for(int i=0;i<30;++i)check(!chord.update(true,true,true),"held chord repeated");
        check(!chord.update(true,false,true),"partial release rearmed chord");
        check(!chord.update(true,true,true),"partial release allowed another toggle");
        chord.update(true,false,false);
        check(chord.update(true,true,true),"second full press failed");
        chord.update(false,false,false);
        check(!chord.update(true,true,true),"focus recovery toggled held buttons");
    });
    test("flat screen command uses the native camera without requiring tracked eyes", [] {
        native_eyes::Command command;
        command.serial=1;command.enabled=2;
        check(native_eyes::valid(command),"native flat camera command rejected");
        command.enabled=3;check(!native_eyes::valid(command),"unknown camera mode accepted");
        command.enabled=2;command.leaseMs=0;
        check(!native_eyes::valid(command),"flat camera without a lease accepted");
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
        // In VR the controllers swing: of their own buttons only pause (menu) and the game menu (Y) pass.
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
        // B is the game's Y in VR (interact, web strike), as the swing leaves it; on the screen B stays B.
        check(fromControllers(f,Mapping::gameplay,{0,0,false,true}).buttons==(start|back|y),"interact is not the game's Y");
        XrFrame pressed;pressed.buttons=buttonB;
        check(fromControllers(pressed,Mapping::menus).buttons==b,"B on the game screen is not the game's B");
        check(fromControllers(pressed,Mapping::gameplay)==game_pad::State{},"B reached the game without the swing's leave");
        XrFrame broken;
        broken.hands[0].stickX=std::numeric_limits<float>::quiet_NaN();
        broken.hands[1].trigger=std::numeric_limits<float>::infinity();
        check(fromControllers(broken,Mapping::menus)==game_pad::State{},"bad readings stay at rest");
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
                        // Every joint goes `fist` of the way from the game's bend to the fist's, about its hinge.
                        for(const auto& f:fingers)
                            for(size_t k=1;k<=3;++k){
                                const float was=bendOf(b,from,f,k,heroHinge(b,from,f,side));
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
                        }
                    }
                }
            }
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
        check(e.knockback==Knockback::Knockdown&&e.damage>25&&e.damage<35,"a firm punch did the wrong harm");
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
        near(shot.origin.z, -.08f);
        check(!shot.target && shot.surface && shot.hand == 1, "no thug: the wall");
        near(shot.aimPoint.z, -30.4f);
        near(shot.aimPoint.y, 1.5f);
        const float up = 1.5707964f;
        shot = s.aim(1, {{0, 1.5f, 0}, {std::sin(up / 2), 0, 0, std::cos(up / 2)}}, {}, world);
        check(!shot.surface && !shot.target, "the sky is open air");
        near(shot.aimPoint.y, 1.5f + .08f + 60, .01f);
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
    test("VR settings tab: four sections, every setting once, a list's choices one per step", [] {
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
        check(headings == 4 && all[0].item == Item::none && all[6].item == Item::none && all[8].item == Item::none &&
                  all[14].item == Item::none,
              "the sections: webs, body, comfort, experimental");
        check(all[15].item == Item::flips && all[15].choices.empty() && std::strcmp(all[14].title, "EXPERIMENTAL") == 0,
              "the flips: a switch under EXPERIMENTAL, the last row");
        check(all[12].item == Item::screenSize && all[13].item == Item::hud && all[13].choices.size() == 4 &&
                  std::strcmp(all[13].choices[0], "OFF") == 0 && std::strcmp(all[13].choices[3], "LARGE") == 0,
              "the HUD after the game screen's size: OFF, SMALL, MEDIUM, LARGE");
        check(all[1].item == Item::webButton && all[1].choices.size() == 2 &&
                  std::strcmp(all[1].choices[0], "GRIP") == 0 && std::strcmp(all[1].choices[1], "TRIGGER") == 0,
              "the web button first under WEBS: GRIP, or TRIGGER");
        check(all[4].item == Item::swingSpeed && all[5].item == Item::weight, "the weight under the swing speed");
        check(all[7].item == Item::calibrate && all[7].choices.size() == 2 &&
                  std::strcmp(all[7].choices[1], "ON RESUME") == 0,
              "the body's calibration: NO, or ON RESUME");
        check(seen == (2u << static_cast<unsigned>(lastItem)) - 2, "a setting missing");
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
    });
    test("VR settings choices: switches and lists round-trip, launcher values show the nearest step", [] {
        using namespace vr_settings;
        const Values defaults;
        check(choice(Item::aimMarkers, defaults) == 1 && choice(Item::swingSpeed, defaults) == 4 &&
                  choice(Item::snapTurn, defaults) == 2 && choice(Item::smoothTurn, defaults) == 0 &&
                  choice(Item::haptics, defaults) == 4 && choice(Item::screenSize, defaults) == 1 &&
                  choice(Item::weight, defaults) == 2,
              "the defaults as the tab shows them: ON, 32 m/s, 30 degrees, no smooth turning, 100%, medium, 80%");
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
        check(choose(Item::weight, 3, v) && v.weight == 100 && choice(Item::weight, v) == 3, "real gravity");
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
        check(!defaults.triggerWebs && choice(Item::webButton, defaults) == 0 && defaultChoice(Item::webButton) == 0 &&
                  choose(Item::webButton, 1, v) && v.triggerWebs && choice(Item::webButton, v) == 1 &&
                  !choose(Item::webButton, 1, v) && !choose(Item::webButton, 2, v),
              "the web button: GRIP by default, TRIGGER swaps it");
        check(defaults.hud == 2 && choice(Item::hud, defaults) == 2 && defaultChoice(Item::hud) == 2 &&
                  choose(Item::hud, 0, v) && v.hud == 0 && choice(Item::hud, v) == 0 && choose(Item::hud, 3, v) &&
                  v.hud == 3 && !choose(Item::hud, 4, v) && !choose(Item::hud, -1, v),
              "the HUD: MEDIUM by default, OFF to LARGE");
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
        check(c.version == 3 && c.bytes == 184 && game_swing::valid(c), "a level version 3 command was rejected");
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
        check(!classOf(other), "an unrelated class was taken for something");
        static const uintptr_t plain[2]{};
        check(!game_targets::classOf(base, reinterpret_cast<uintptr_t>(&plain[1])),
              "data was taken for a class");
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

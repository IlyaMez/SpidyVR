#include "spidy/lab_world.hpp"
#include "spidy/native_view.hpp"
#include "spidy/game_tracking.hpp"
#include "spidy/native_eye_frame.hpp"
#include "spidy/native_eye_history.hpp"
#include "spidy/eye_job_table.hpp"
#include "spidy/eye_pair_state.hpp"
#include "spidy/native_rays.hpp"
#include "spidy/game_swing.hpp"
#include "spidy/native_movement.hpp"
#include "spidy/presentation_gate.hpp"
#include "spidy/eye_resolution.hpp"
#include "spidy/vr_shortcut.hpp"
#include "spidy/web_visual.hpp"
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
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
SwingConfig inert() {
    SwingConfig c;
    c.gravity = 0;
    c.airAcceleration = 0;
    return c;
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
    test("held web gesture retries when aim finds a surface", [] {
        TestWorld world;world.enabled=false;auto config=inert();config.airAnchors=false;
        Swing swing(config);auto in=aimed();
        Body actual{{0,1,0},{},true};
        swing.predictNativeStep(.01f,in,world,actual);
        check(!swing.webs()[0].attached,"miss attached");
        world.enabled=true;
        for(int i=0;i<20;++i)swing.predictNativeStep(.01f,in,world,actual);
        check(swing.webs()[0].attached,"held gesture required a second button press");
        in.hands[0].grip=0;swing.predictNativeStep(.01f,in,world,actual);
        check(!swing.webs()[0].attached,"grip release failed");
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
    test("artificial player movement does not become a hand yank", [] {
        GameTrackingRig rig;auto f=trackedFrame();rig.update(f,{},{0,0,-1},true);
        f.predictedDisplayTime=2;const auto a=rig.update(f,{},{0,0,-1},true);
        f.predictedDisplayTime=3;const auto b=rig.update(f,{20,3,-4},{0,0,-1},true);
        near(b.head[12]-a.head[12],20);near(b.head[13]-a.head[13],3);
        near(length(b.swing.hands[0].gripRelativeToHead-a.swing.hands[0].gripRelativeToHead),0);
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
        w.enabled=true;w.anchor={0,10,0};clear.predictNativeStep(.01f,in,w,body);
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
            for (unsigned i = 0; i < static_cast<unsigned>(hz); ++i) {
                const auto previous = actual.position;
                const auto result = swing.predictNativeStep(dt, in, world, actual);
                check(result.valid && swing.webs()[0].attached, "native swing lost its web");
                actual.position = result.target;
                actual.velocity = (result.target - previous) / dt;
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
                actual.velocity = (result.target - actual.position) / dt;
                actual.position = result.target;
            }
            check(actual.velocity.z > 1, "air steering did not respond promptly");
        }
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
        check(validEyeSize(1536) && validEyeSize(2048) && validEyeSize(4096),"high resolution rejected");
        check(!validEyeSize(0) && !validEyeSize(63) && !validEyeSize(4097),"invalid eye size accepted");
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
    test("movement requires a recently submitted headset image", [] {
        check(!recentPresentation(0,1000),"controls started before a first image");
        check(recentPresentation(1000,1000),"current image rejected");
        check(recentPresentation(1000,1250),"valid image lease rejected");
        check(!recentPresentation(1000,1251),"stalled presentation kept controls active");
        check(!recentPresentation(1000,999),"clock reversal accepted");
        check(recentPresentation(1300,1301),"new image did not restore the gate");
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
        check(history.find(7,1150000000),"valid image age boundary rejected");
        check(!history.find(7,1150000001),"stale image retained");
        for(uint64_t i=8;i<=263;++i) {
            frame.serial=i;
            history.remember(frame);
        }
        check(!history.find(7,1000000000),"overwritten pose silently used for an old image");
        check(history.find(263,1000000000),"new ring entry lost");
        history.clear();
        check(!history.find(263,1000000000),"pre-recenter render pose retained");
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
    std::cout << total - failed << '/' << total << " tests passed\n";
    return failed ? 1 : 0;
}

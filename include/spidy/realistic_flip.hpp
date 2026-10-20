#pragma once
#include "tracking.hpp"

namespace spidy {
struct RealisticFlipSettings {
    bool enabled = false, releaseFlip = true; // carry swing momentum on release
    int strength = 50, speed = 240, minimumSpeed = 3;
    int responseMs = 500, handStrength = 100, falloffMs = 3000;
    int handThreshold = 100; // centimetres per second
    bool airBrake = true;
    bool operator==(const RealisticFlipSettings&) const = default;
};
struct RealisticFlight {
    Vec3 velocity{}, pull{};
    bool valid{}, attached{};
    Vec3 handMomentum{}; // raw tracking-space arm movement, before virtual rotation
    float ropeLength = 20;
};
struct RealisticControls {
    bool brake{};
    Vec3 manualAngular{}; // tracking-local left-stick angular velocity
    bool manualActive{};
};
// Read physical tracking positions, not world-transformed virtual hands. This
// excludes locomotion, virtual flips and common head/hand translation.
class AutomaticHandMotion {
    Vec3 previous_[2]{}, filtered_[2]{};
    bool sampled_[2]{};
  public:
    void reset() { *this={}; }
    Vec3 update(const XrFrame& frame,int threshold) {
        if(!validTrackedPose(frame.head) || !std::isfinite(frame.seconds) ||
           frame.seconds<=0 || frame.seconds>.1f) { reset(); return {}; }
        Vec3 result{};
        const float dt=frame.seconds;
        for(unsigned i=0;i<2;++i) {
            const auto& hand=frame.hands[i];
            if(!hand.valid || !validTrackedPose(hand.grip)) {
                sampled_[i]=false; filtered_[i]={}; continue;
            }
            const Vec3 relative=hand.grip.position-frame.head.position;
            if(sampled_[i]) {
                const Vec3 delta=relative-previous_[i];
                const Vec3 velocity=delta/dt;
                // Discontinuities are tracking jumps, not an arm impulse.
                if(length(delta)<.35f && length(velocity)<12.f) {
                    filtered_[i]+=(velocity-filtered_[i])*(1-std::exp(-dt/.06f));
                    const float speed=length(filtered_[i]);
                    const float minimum=std::clamp(threshold,50,300)/100.f;
                    const float weight=speed>minimum ? 1-minimum/speed : 0;
                    const Vec3 arm=relative+Vec3{0,.35f,0};
                    result+=cross(arm,filtered_[i])*weight;
                } else filtered_[i]={};
            }
            previous_[i]=relative; sampled_[i]=true;
        }
        return limited(result,8.f);
    }
};
// Continuous angular velocity: there is no target turn count, flip timer or
// canned animation. Hand motion adds momentum; air drag removes it. A tether
// supplies a smooth alignment torque while attached, and release carries spin.
class RealisticFlip {
    Quat tilt_{}, levelTarget_{};
    Vec3 angular_{}, momentum_{}, averageVelocity_{}, lastPull_{0,1,0};
    bool attached_{}, sampled_{}, airborne_{}, leveling_{}, brakeHeld_{};
    float missingTime_{}, lastRadius_=20;
    static Quat unit(Quat q) {
        const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
        return n>1e-6f ? Quat{q.x/n,q.y/n,q.z/n,q.w/n} : Quat{};
    }
    static Vec3 rotationVector(Quat q) {
        q=unit(q); if(q.w<0) q={-q.x,-q.y,-q.z,-q.w};
        const Vec3 v{q.x,q.y,q.z}; const float n=length(v);
        return n>1e-6f ? v*(2*std::atan2(n,std::max(0.f,q.w))/n) : Vec3{};
    }
    void integrate(Vec3 desired,float acceleration,float dt) {
        const Vec3 before=angular_;
        angular_+=limited(desired-angular_,acceleration*dt);
        const Vec3 step=(before+angular_)*(.5f*dt);
        const float angle=length(step);
        if(angle>1e-7f) tilt_=unit(Quat::around(step,angle)*tilt_);
    }
  public:
    void reset() { *this={}; }
    Quat orientation() const { return tilt_; }
    bool recovering() const { return leveling_; }
    bool level() const { return std::abs(tilt_.w)>.99999f; }
    Vec3 angularVelocity() const { return angular_; }
    Quat update(RealisticFlight flight,bool airborne,bool surface,float yaw,float dt,
                RealisticFlipSettings settings,RealisticControls controls={}) {
        if(!settings.enabled) { reset(); return {}; }
        if(!std::isfinite(dt) || dt<=0 || dt>.1f) return tilt_;
        const float autoRate=std::clamp(settings.speed,90,360)*3.14159265f/180;
        const bool manual=controls.manualActive && finite(controls.manualAngular);
        const float rate=manual ? std::max(autoRate,length(controls.manualAngular)) : autoRate;
        const bool brake=controls.brake && settings.airBrake;
        if(brake && !brakeHeld_) {
            levelTarget_=unit(arc(tilt_.rotate({0,1,0}),{0,1,0})*tilt_);
            leveling_=true;
        }
        brakeHeld_=brake;
        if(brake || leveling_) {
            // Finish an upright recovery even after a short button press.
            // Preserve horizontal heading, and discard the old spin momentum.
            momentum_={};
            const Vec3 error=rotationVector(levelTarget_*tilt_.conjugate());
            integrate(limited(error/.18f,autoRate),autoRate/.12f,dt);
            if(length(error)<.001f && length(angular_)<.02f) {
                tilt_=levelTarget_; angular_={}; leveling_=false;
            }
            attached_=airborne && !surface && flight.attached;
            airborne_=airborne && !surface;
            return tilt_;
        }
        const float response=std::clamp(settings.responseMs,100,750)/1000.f;
        const float acceleration=rate/response;
        const bool valid=flight.valid && finite(flight.velocity) && finite(flight.pull) && finite(flight.handMomentum);
        if(!valid) {
            missingTime_+=dt;
            if(missingTime_<=.25f) return tilt_;
            attached_=airborne_=false; momentum_={};
            integrate(limited(rotationVector(arc(tilt_.rotate({0,1,0}),{0,1,0}))/response,rate),acceleration,dt);
            return tilt_;
        }
        missingTime_=0;
        const Quat local=Quat::yaw(-yaw);
        const Vec3 velocity=local.rotate(flight.velocity);
        if(!sampled_) { averageVelocity_=velocity; sampled_=true; }
        averageVelocity_+=(velocity-averageVelocity_)*(1-std::exp(-dt/.12f));
        const bool flying=airborne && !surface;
        const bool tether=flying && flight.attached && length(flight.pull)>.01f;
        const bool enough=length(averageVelocity_)>=std::clamp(settings.minimumSpeed,3,16);
        const float strength=std::clamp(settings.strength,25,100)/100.f;
        const float drag=std::clamp(settings.falloffMs,500,3000)/1000.f;
        Vec3 desired=momentum_*std::exp(-dt/drag);
        if(tether) {
            lastPull_=normalized(local.rotate(flight.pull));
            if(std::isfinite(flight.ropeLength)) lastRadius_=std::clamp(flight.ropeLength,2.f,100.f);
            if(enough) {
                const auto delta=arc(tilt_.rotate({0,1,0}),lastPull_);
                desired=limited(rotationVector(delta)*(strength/response),rate);
            }
        } else if(flying && attached_ && settings.releaseFlip) {
            // Tangential orbit momentum supplements the rotation already present.
            // No prescribed 360-degree operation is started here.
            const Vec3 orbit=cross(-lastPull_,averageVelocity_)/lastRadius_;
            desired=angular_+limited(orbit*strength,rate*.5f);
        }
        if(flying) {
            const float gain=8.f*std::clamp(settings.handStrength,25,200)/100.f;
            const Vec3 hand=tilt_.rotate(flight.handMomentum)*gain;
            // Capture the short arm swing that straddles takeoff, then accept
            // ongoing gestures and opposite gestures that brake the rotation.
            desired+=hand*(dt+(!airborne_ ? .08f : 0));

        } else {
            desired=limited(rotationVector(arc(tilt_.rotate({0,1,0}),{0,1,0}))/response,rate);
        }
        if(manual && flying) desired=tilt_.rotate(controls.manualAngular);
        attached_=tether; airborne_=flying;
        momentum_=limited(desired,rate);
        integrate(momentum_,acceleration,dt);
        if(tether || !flying || manual) momentum_=angular_;
        return tilt_;
    }
};
} // namespace spidy

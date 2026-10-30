#pragma once
#include "math.hpp"
#include <algorithm>

namespace spidy {
// Smooth only the recording camera's rotation. Translation stays current so
// swinging cannot make the camera trail through the player's body or walls.
class SpectatorSmoothing {
    Mat4 previous_{};
    bool ready_{};
  public:
    void reset() { ready_ = false; }
    Mat4 update(Mat4 target, float seconds, bool enabled, int amountMs = 150) {
        if (!enabled || !ready_ || !std::isfinite(seconds) || seconds < 0 || seconds > .25f ||
            length(Vec3{target[12]-previous_[12],target[13]-previous_[13],target[14]-previous_[14]}) > 3.f) {
            previous_ = target; ready_ = enabled;
            return target;
        }
        if (seconds == 0) {
            // A second view submission in the same frame must not discard history.
            for (int row=0;row<3;++row)
                for (int col=0;col<3;++col) target[row*4+col]=previous_[row*4+col];
            return target;
        }
        const Vec3 oldX{previous_[0],previous_[1],previous_[2]};
        const Vec3 oldZ{previous_[8],previous_[9],previous_[10]};
        const Vec3 nextX{target[0],target[1],target[2]}, nextZ{target[8],target[9],target[10]};
        Quat delta = arc(oldZ,nextZ);
        const Vec3 alignedX = delta.rotate(oldX);
        const float twist = std::atan2(dot(nextZ,cross(alignedX,nextX)),dot(alignedX,nextX));
        delta = Quat::around(nextZ,twist) * delta;
        if (delta.w < 0) delta = {-delta.x,-delta.y,-delta.z,-delta.w};
        const Vec3 axis{delta.x,delta.y,delta.z};
        const float angle = 2*std::atan2(length(axis),std::max(0.f,delta.w));
        const float response = std::clamp(amountMs,25,500)/1000.f;
        const Quat step = Quat::around(axis,angle*(1-std::exp(-seconds/response)));
        // Retain pitch, yaw and roll equally; there is no upright/horizon target.
        for (int row=0;row<3;++row) {
            const int at=row*4;
            const Vec3 v=step.rotate({previous_[at],previous_[at+1],previous_[at+2]});
            target[at]=v.x; target[at+1]=v.y; target[at+2]=v.z;
        }
        previous_=target;
        return target;
    }
};
}

#pragma once
// Camera in render space (x east, y up, z north; double precision position) and controllers.
#include "core/Math.h"

namespace it::render {

/// Simulation (x east, y north, z up) to render space (x east, y up, z north).
inline Vec3 toRender(const Vec3& sim) { return {sim.x, sim.z, sim.y}; }
inline Vec3 toRender(const Vec2& sim, double up) { return {sim.x, up, sim.y}; }

struct Camera {
    Vec3 pos{0, 30, 0};
    double yaw = 0;    // nautical bearing the camera looks towards (deg)
    double pitch = -5; // deg, negative = looking down
    double fovY = 50;  // deg
    double nearZ = 0.5;

    Vec3f forward() const {
        double cp = std::cos(pitch * kDegToRad), sp = std::sin(pitch * kDegToRad);
        return {float(std::sin(yaw * kDegToRad) * cp), float(sp), float(std::cos(yaw * kDegToRad) * cp)};
    }
    /// Rotation-only view matrix (positions are camera-relative).
    Mat4f view() const { return Mat4f::lookAtLH({0, 0, 0}, forward(), {0, 1, 0}); }
    Mat4f proj(float aspect) const { return Mat4f::perspectiveReversedInfLH(float(fovY * kDegToRad), aspect, float(nearZ)); }
    /// World-space ray through a screen point (pixels).
    void ray(float sx, float sy, float w, float h, Vec3& origin, Vec3f& dir) const {
        Mat4f inv = (proj(w / h) * view()).inverse();
        float nx = sx / w * 2.0f - 1.0f, ny = 1.0f - sy / h * 2.0f;
        Vec4f p = inv * Vec4f(nx, ny, 0.5f, 1.0f);
        dir = normalize(Vec3f{p.x / p.w, p.y / p.w, p.z / p.w});
        origin = pos;
    }
};

enum class CameraMode { Orbit, Free, Bridge, Binoculars, Cinematic };

/// Orbit / free / bridge camera driven by mouse and keyboard deltas (platform independent).
struct CameraController {
    CameraMode mode = CameraMode::Orbit;
    Vec3 target;            // orbit target (render space)
    double distance = 600;  // orbit distance (m)
    double orbitYaw = 220, orbitPitch = -12;
    double minHeight = 2.0;

    void orbit(double dx, double dy) {
        orbitYaw = wrap360(orbitYaw + dx * 0.25);
        orbitPitch = clamp(orbitPitch - dy * 0.2, -88.0, 10.0);
    }
    void zoom(double wheel) { distance = clamp(distance * std::pow(0.88, wheel), 25.0, 60000.0); }
    void apply(Camera& cam, double waterHeight = 0.0) const {
        if (mode == CameraMode::Orbit || mode == CameraMode::Cinematic) {
            double cp = std::cos(orbitPitch * kDegToRad), sp = std::sin(orbitPitch * kDegToRad);
            Vec3 dir{std::sin(orbitYaw * kDegToRad) * cp, sp, std::cos(orbitYaw * kDegToRad) * cp};
            cam.pos = target - dir * distance;
            cam.pos.y = std::max(cam.pos.y, waterHeight + minHeight);
            Vec3 look = target - cam.pos;
            cam.yaw = bearingOf({look.x, look.z});
            cam.pitch = std::atan2(look.y, std::sqrt(look.x * look.x + look.z * look.z)) * kRadToDeg;
            cam.fovY = 50;
        }
    }
};

}  // namespace it::render

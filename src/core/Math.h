#pragma once
// Small, dependency-free math library shared by simulation, procedural generation and renderer.
// Conventions (simulation): x = east, y = north, z = up, metres. Bearings/headings are nautical:
// degrees clockwise from north, in [0, 360).
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace it {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kGravity = 9.80665;

template <typename T> constexpr T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
template <typename T> constexpr T saturate(T v) { return clamp<T>(v, T(0), T(1)); }
template <typename T> constexpr T lerp(T a, T b, T t) { return a + (b - a) * t; }
template <typename T> constexpr T sqr(T v) { return v * v; }
inline double smoothstep(double e0, double e1, double x) {
    double t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.0 - 2.0 * t);
}
/// Linear remap of x from [a0,a1] to [b0,b1] with clamping.
inline double remap(double x, double a0, double a1, double b0, double b1) {
    if (a1 == a0) return b0;
    return lerp(b0, b1, saturate((x - a0) / (a1 - a0)));
}

/// Wrap angle in degrees to [0, 360).
inline double wrap360(double deg) {
    deg = std::fmod(deg, 360.0);
    return deg < 0.0 ? deg + 360.0 : deg;
}
/// Wrap angle in degrees to (-180, 180].
inline double wrap180(double deg) {
    deg = wrap360(deg);
    return deg > 180.0 ? deg - 360.0 : deg;
}
/// Signed smallest difference to - from in degrees, (-180, 180].
inline double angleDiff(double fromDeg, double toDeg) { return wrap180(toDeg - fromDeg); }

template <typename T> struct Vec2T {
    T x{}, y{};
    constexpr Vec2T() = default;
    constexpr Vec2T(T x_, T y_) : x(x_), y(y_) {}
    template <typename U> explicit constexpr Vec2T(const Vec2T<U>& o) : x(T(o.x)), y(T(o.y)) {}
    constexpr Vec2T operator+(const Vec2T& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2T operator-(const Vec2T& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2T operator*(T s) const { return {x * s, y * s}; }
    constexpr Vec2T operator/(T s) const { return {x / s, y / s}; }
    constexpr Vec2T operator-() const { return {-x, -y}; }
    Vec2T& operator+=(const Vec2T& o) { x += o.x; y += o.y; return *this; }
    Vec2T& operator-=(const Vec2T& o) { x -= o.x; y -= o.y; return *this; }
    Vec2T& operator*=(T s) { x *= s; y *= s; return *this; }
    constexpr bool operator==(const Vec2T&) const = default;
};
template <typename T> constexpr Vec2T<T> operator*(T s, const Vec2T<T>& v) { return v * s; }
template <typename T> constexpr T dot(const Vec2T<T>& a, const Vec2T<T>& b) { return a.x * b.x + a.y * b.y; }
template <typename T> constexpr T cross(const Vec2T<T>& a, const Vec2T<T>& b) { return a.x * b.y - a.y * b.x; }
template <typename T> inline T length(const Vec2T<T>& v) { return std::sqrt(dot(v, v)); }
template <typename T> constexpr T lengthSq(const Vec2T<T>& v) { return dot(v, v); }
template <typename T> inline Vec2T<T> normalize(const Vec2T<T>& v) {
    T l = length(v);
    return l > T(0) ? v / l : Vec2T<T>{};
}
template <typename T> inline T distance(const Vec2T<T>& a, const Vec2T<T>& b) { return length(b - a); }
template <typename T> constexpr T distanceSq(const Vec2T<T>& a, const Vec2T<T>& b) { return lengthSq(b - a); }
template <typename T> inline Vec2T<T> lerp(const Vec2T<T>& a, const Vec2T<T>& b, T t) { return a + (b - a) * t; }

template <typename T> struct Vec3T {
    T x{}, y{}, z{};
    constexpr Vec3T() = default;
    constexpr Vec3T(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}
    constexpr Vec3T(const Vec2T<T>& v, T z_) : x(v.x), y(v.y), z(z_) {}
    template <typename U> explicit constexpr Vec3T(const Vec3T<U>& o) : x(T(o.x)), y(T(o.y)), z(T(o.z)) {}
    constexpr Vec2T<T> xy() const { return {x, y}; }
    constexpr Vec3T operator+(const Vec3T& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3T operator-(const Vec3T& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3T operator*(T s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3T operator*(const Vec3T& o) const { return {x * o.x, y * o.y, z * o.z}; }
    constexpr Vec3T operator/(T s) const { return {x / s, y / s, z / s}; }
    constexpr Vec3T operator-() const { return {-x, -y, -z}; }
    Vec3T& operator+=(const Vec3T& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3T& operator-=(const Vec3T& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3T& operator*=(T s) { x *= s; y *= s; z *= s; return *this; }
    T& operator[](int i) { return (&x)[i]; }
    const T& operator[](int i) const { return (&x)[i]; }
    constexpr bool operator==(const Vec3T&) const = default;
};
template <typename T> constexpr Vec3T<T> operator*(T s, const Vec3T<T>& v) { return v * s; }
template <typename T> constexpr T dot(const Vec3T<T>& a, const Vec3T<T>& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
template <typename T> constexpr Vec3T<T> cross(const Vec3T<T>& a, const Vec3T<T>& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
template <typename T> inline T length(const Vec3T<T>& v) { return std::sqrt(dot(v, v)); }
template <typename T> constexpr T lengthSq(const Vec3T<T>& v) { return dot(v, v); }
template <typename T> inline Vec3T<T> normalize(const Vec3T<T>& v) {
    T l = length(v);
    return l > T(0) ? v / l : Vec3T<T>{};
}
template <typename T> inline T distance(const Vec3T<T>& a, const Vec3T<T>& b) { return length(b - a); }
template <typename T> constexpr T distanceSq(const Vec3T<T>& a, const Vec3T<T>& b) { return lengthSq(b - a); }
template <typename T> inline Vec3T<T> lerp(const Vec3T<T>& a, const Vec3T<T>& b, T t) { return a + (b - a) * t; }
template <typename T> inline Vec3T<T> vmin(const Vec3T<T>& a, const Vec3T<T>& b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
template <typename T> inline Vec3T<T> vmax(const Vec3T<T>& a, const Vec3T<T>& b) {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

template <typename T> struct Vec4T {
    T x{}, y{}, z{}, w{};
    constexpr Vec4T() = default;
    constexpr Vec4T(T x_, T y_, T z_, T w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4T(const Vec3T<T>& v, T w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    constexpr Vec3T<T> xyz() const { return {x, y, z}; }
    T& operator[](int i) { return (&x)[i]; }
    const T& operator[](int i) const { return (&x)[i]; }
};

using Vec2 = Vec2T<double>;
using Vec3 = Vec3T<double>;
using Vec2f = Vec2T<float>;
using Vec3f = Vec3T<float>;
using Vec4f = Vec4T<float>;

/// Unit vector for a nautical bearing (deg clockwise from north) in the sim's x=east/y=north plane.
inline Vec2 bearingVector(double bearingDeg) {
    double r = bearingDeg * kDegToRad;
    return {std::sin(r), std::cos(r)};
}
/// Nautical bearing in degrees of the vector d (x=east, y=north).
inline double bearingOf(const Vec2& d) { return wrap360(std::atan2(d.x, d.y) * kRadToDeg); }
inline double bearingTo(const Vec2& from, const Vec2& to) { return bearingOf(to - from); }

/// Quaternion (x,y,z,w), float precision for rendering & model transforms.
struct Quatf {
    float x = 0, y = 0, z = 0, w = 1;
    static Quatf axisAngle(const Vec3f& axis, float radians) {
        Vec3f a = normalize(axis);
        float s = std::sin(radians * 0.5f);
        return {a.x * s, a.y * s, a.z * s, std::cos(radians * 0.5f)};
    }
    Quatf operator*(const Quatf& q) const {
        return {w * q.x + x * q.w + y * q.z - z * q.y, w * q.y - x * q.z + y * q.w + z * q.x,
                w * q.z + x * q.y - y * q.x + z * q.w, w * q.w - x * q.x - y * q.y - z * q.z};
    }
    Vec3f rotate(const Vec3f& v) const {
        Vec3f u{x, y, z};
        Vec3f t = cross(u, v) * 2.0f;
        return v + t * w + cross(u, t);
    }
};

/// Column-major 4x4 matrix (m[col][row]); vectors are columns: v' = M * v.
/// HLSL constant buffers receive it transposed-free when shaders use mul(M, v) with column_major.
struct Mat4f {
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

    static Mat4f identity() { return {}; }
    static Mat4f translation(const Vec3f& t) {
        Mat4f r;
        r.m[3][0] = t.x; r.m[3][1] = t.y; r.m[3][2] = t.z;
        return r;
    }
    static Mat4f scale(const Vec3f& s) {
        Mat4f r;
        r.m[0][0] = s.x; r.m[1][1] = s.y; r.m[2][2] = s.z;
        return r;
    }
    static Mat4f rotation(const Quatf& q) {
        Mat4f r;
        float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        r.m[0][0] = 1 - 2 * (yy + zz); r.m[0][1] = 2 * (xy + wz);     r.m[0][2] = 2 * (xz - wy);
        r.m[1][0] = 2 * (xy - wz);     r.m[1][1] = 1 - 2 * (xx + zz); r.m[1][2] = 2 * (yz + wx);
        r.m[2][0] = 2 * (xz + wy);     r.m[2][1] = 2 * (yz - wx);     r.m[2][2] = 1 - 2 * (xx + yy);
        return r;
    }
    static Mat4f trs(const Vec3f& t, const Quatf& q, const Vec3f& s) {
        return translation(t) * rotation(q) * scale(s);
    }
    /// Left-handed look-at (D3D convention: +z forward in view space).
    static Mat4f lookAtLH(const Vec3f& eye, const Vec3f& target, const Vec3f& up) {
        Vec3f f = normalize(target - eye);
        Vec3f s = normalize(cross(up, f));
        Vec3f u = cross(f, s);
        Mat4f r;
        r.m[0][0] = s.x; r.m[1][0] = s.y; r.m[2][0] = s.z;
        r.m[0][1] = u.x; r.m[1][1] = u.y; r.m[2][1] = u.z;
        r.m[0][2] = f.x; r.m[1][2] = f.y; r.m[2][2] = f.z;
        r.m[3][0] = -dot(s, eye); r.m[3][1] = -dot(u, eye); r.m[3][2] = -dot(f, eye);
        return r;
    }
    /// Left-handed perspective with reversed-Z and infinite far plane (near maps to 1, infinity to 0).
    static Mat4f perspectiveReversedInfLH(float fovY, float aspect, float zNear) {
        float h = 1.0f / std::tan(fovY * 0.5f);
        Mat4f r;
        r.m[0][0] = h / aspect; r.m[1][1] = h;
        r.m[2][2] = 0.0f; r.m[2][3] = 1.0f;
        r.m[3][2] = zNear; r.m[3][3] = 0.0f;
        return r;
    }
    static Mat4f orthoLH(float w, float h, float zn, float zf) {
        Mat4f r;
        r.m[0][0] = 2.0f / w; r.m[1][1] = 2.0f / h;
        r.m[2][2] = 1.0f / (zf - zn); r.m[3][2] = -zn / (zf - zn);
        return r;
    }
    Mat4f operator*(const Mat4f& b) const {
        Mat4f r;
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[k][rr] * b.m[c][k];
                r.m[c][rr] = s;
            }
        return r;
    }
    Vec4f operator*(const Vec4f& v) const {
        Vec4f r;
        for (int rr = 0; rr < 4; ++rr) r[rr] = m[0][rr] * v.x + m[1][rr] * v.y + m[2][rr] * v.z + m[3][rr] * v.w;
        return r;
    }
    Vec3f transformPoint(const Vec3f& p) const {
        Vec4f r = (*this) * Vec4f(p, 1.0f);
        return {r.x, r.y, r.z};
    }
    Vec3f transformDir(const Vec3f& d) const {
        Vec4f r = (*this) * Vec4f(d, 0.0f);
        return {r.x, r.y, r.z};
    }
    Mat4f transposed() const {
        Mat4f r;
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) r.m[c][rr] = m[rr][c];
        return r;
    }
    Mat4f inverse() const;
};

/// Axis-aligned bounding box.
struct Aabb {
    Vec3f lo{1e30f, 1e30f, 1e30f};
    Vec3f hi{-1e30f, -1e30f, -1e30f};
    void add(const Vec3f& p) { lo = vmin(lo, p); hi = vmax(hi, p); }
    bool valid() const { return lo.x <= hi.x; }
    Vec3f center() const { return (lo + hi) * 0.5f; }
    Vec3f extent() const { return (hi - lo) * 0.5f; }
};

/// Closest distance between two points moving linearly; returns time of closest approach (>= 0).
inline double closestApproachTime(const Vec2& p0, const Vec2& v0, const Vec2& p1, const Vec2& v1) {
    Vec2 dp = p1 - p0, dv = v1 - v0;
    double vv = dot(dv, dv);
    if (vv < 1e-9) return 0.0;
    return std::max(0.0, -dot(dp, dv) / vv);
}

/// Solve intercept: course for a projectile of speed s from p0 to hit target at pt moving with vt.
/// Returns false if no solution. Outputs time to intercept and aim point.
inline bool solveIntercept(const Vec2& p0, double s, const Vec2& pt, const Vec2& vt, double& tOut, Vec2& aimOut) {
    Vec2 d = pt - p0;
    double a = dot(vt, vt) - s * s;
    double b = 2.0 * dot(d, vt);
    double c = dot(d, d);
    double t;
    if (std::fabs(a) < 1e-9) {
        if (std::fabs(b) < 1e-9) return false;
        t = -c / b;
    } else {
        double disc = b * b - 4 * a * c;
        if (disc < 0) return false;
        double sq = std::sqrt(disc);
        double t1 = (-b - sq) / (2 * a), t2 = (-b + sq) / (2 * a);
        t = 1e30;
        if (t1 > 0) t = t1;
        if (t2 > 0 && t2 < t) t = t2;
        if (t >= 1e29) return false;
    }
    if (t <= 0) return false;
    tOut = t;
    aimOut = pt + vt * t;
    return true;
}

}  // namespace it

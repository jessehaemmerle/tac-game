#include "core/Math.h"

namespace it {

Mat4f Mat4f::inverse() const {
    // Gauss-Jordan on a row-major copy (a[r][c] = m[c][r]).
    double a[4][8];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            a[r][c] = m[c][r];
            a[r][c + 4] = (r == c) ? 1.0 : 0.0;
        }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 4; ++r)
            if (std::fabs(a[r][col]) > std::fabs(a[pivot][col])) pivot = r;
        if (std::fabs(a[pivot][col]) < 1e-12) return identity();
        if (pivot != col)
            for (int c = 0; c < 8; ++c) std::swap(a[pivot][c], a[col][c]);
        double inv = 1.0 / a[col][col];
        for (int c = 0; c < 8; ++c) a[col][c] *= inv;
        for (int r = 0; r < 4; ++r) {
            if (r == col) continue;
            double f = a[r][col];
            if (f == 0.0) continue;
            for (int c = 0; c < 8; ++c) a[r][c] -= f * a[col][c];
        }
    }
    Mat4f out;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out.m[c][r] = float(a[r][c + 4]);
    return out;
}

}  // namespace it

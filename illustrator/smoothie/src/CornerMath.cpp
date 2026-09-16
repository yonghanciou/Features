#include "CornerMath.h"

#include <algorithm>
#include <cmath>

namespace smoothie {

double Vec2::length() const { return std::sqrt(x * x + y * y); }

Vec2 Vec2::normalized() const {
    double l = length();
    if (l < 1e-12) return {0.0, 0.0};
    return {x / l, y / l};
}

static double Clamp(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

double CurvatureAtStart(const BezierSeg& seg) {
    Vec2 v1 = seg.p1 - seg.p0;
    Vec2 v2 = seg.p2 - seg.p1;
    double l1 = v1.length();
    if (l1 < 1e-12) return 0.0;
    return (2.0 / 3.0) * (v1.cross(v2)) / (l1 * l1 * l1);
}

double CurvatureAtEnd(const BezierSeg& seg) {
    // Curvature at t=1 by symmetry: treat the curve traversed backwards
    // (p3 -> p2 -> p1 -> p0) and reuse the start-curvature formula.
    BezierSeg rev{seg.p3, seg.p2, seg.p1, seg.p0};
    return CurvatureAtStart(rev);
}

double CurvatureAt(const BezierSeg& seg, double t) {
    double mt = 1.0 - t;
    Vec2 d1 = (seg.p1 - seg.p0) * (3.0 * mt * mt)
            + (seg.p2 - seg.p1) * (6.0 * mt * t)
            + (seg.p3 - seg.p2) * (3.0 * t * t);
    Vec2 d2 = (seg.p2 - seg.p1 * 2.0 + seg.p0) * (6.0 * mt)
            + (seg.p3 - seg.p2 * 2.0 + seg.p1) * (6.0 * t);
    double speed = d1.length();
    if (speed < 1e-12) return 0.0;
    return d1.cross(d2) / (speed * speed * speed);
}

// Glyphs-style Curvature (see CornerMath.h for the derivation): the handle
// length is a direct proportion of the anchor distance D, not an
// interpolation toward any circle-approximation constant.
CornerResult ComputeCorner(const Vec2& A, const Vec2& B, const Vec2& C, const CornerParams& params) {
    CornerResult result;

    // V = B. u1/u2 = outward unit vectors from V toward A/C.
    Vec2 rawU1 = A - B;
    Vec2 rawU2 = C - B;
    double lenAB = rawU1.length();
    double lenBC = rawU2.length();
    if (lenAB < 1e-9 || lenBC < 1e-9) return result;

    Vec2 u1 = rawU1 * (1.0 / lenAB);
    Vec2 u2 = rawU2 * (1.0 / lenBC);

    double cosTheta = Clamp(u1.dot(u2), -1.0, 1.0);
    double theta = std::acos(cosTheta); // interior angle at V, in (0, pi)
    if (theta < 1e-4 || theta > M_PI - 1e-4) {
        // Effectively straight, or edges doubling back on themselves:
        // nothing sensible to round.
        return result;
    }

    double R = std::max(params.radius, 0.0);
    if (R < 1e-6) return result;

    // Step 2: anchor distance. D = R / tan(theta/2).
    double D = R / std::tan(theta * 0.5);

    // Defensive clamp -- NOT part of the Glyphs formula itself: on a short
    // edge, D can exceed half the edge length, which would push P1/P2 past
    // the neighboring vertex and self-intersect. Rescale R to match so R
    // and D stay consistent with each other (same clamp shape the old
    // implementation and the standalone Smoothie.jsx script both use).
    double maxD = 0.5 * std::min(lenAB, lenBC);
    if (D > maxD) {
        D = maxD;
        R = D * std::tan(theta * 0.5);
    }
    if (D < 1e-6) return result;

    // Step 4a: anchor points, D out from V along each edge.
    Vec2 P1 = B + u1 * D;
    Vec2 P2 = B + u2 * D;
    result.trimStart = P1;
    result.trimEnd = P2;

    // Step 3: handle length is a straight proportion of D -- no circle
    // constant involved. C in [0,1], so L <= D always (handle never
    // passes V).
    double curvature = Clamp(params.curvaturePercent, 0.0, 1.0);
    double L = D * curvature;

    // Step 4b: handles point from each anchor back toward V, strictly
    // along u1/u2 -- P1,H1 collinear (both on the u1 line through V) and
    // likewise P2,H2 collinear, by construction.
    Vec2 H1 = P1 - u1 * L;
    Vec2 H2 = P2 - u2 * L;

    result.curve = BezierSeg{P1, H1, H2, P2};
    result.valid = true;
    return result;
}

} // namespace smoothie

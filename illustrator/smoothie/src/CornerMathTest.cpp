// Standalone verification harness for CornerMath.
// Build & run (no Illustrator SDK needed):
//   clang++ -std=c++17 -O0 -g CornerMath.cpp CornerMathTest.cpp -o /tmp/corner_test
//   /tmp/corner_test
#include "CornerMath.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace smoothie;

static int g_failures = 0;

static void Check(bool cond, const char* what) {
    if (!cond) {
        std::printf("  [FAIL] %s\n", what);
        g_failures++;
    } else {
        std::printf("  [ok]   %s\n", what);
    }
}

static void CheckNear(double a, double b, double tol, const char* what) {
    bool ok = std::fabs(a - b) <= tol;
    std::printf("  [%s] %s (got %.6f, expected %.6f, tol %.6f)\n",
                ok ? "ok" : "FAIL", what, a, b, tol);
    if (!ok) g_failures++;
}

static double Dist(const Vec2& a, const Vec2& b) {
    return (a - b).length();
}

int main() {
    // --- Case 1: curvaturePercent = 1.0 (100%) -- Glyphs-style: the handle
    // length equals the full anchor distance D, so the handle lands
    // exactly ON the vertex V (H1 == B, H2 == B). There is no circle-kappa
    // reference in this formula at all; that used to be true of the old
    // "vector interpolation" algorithm, not this one.
    {
        std::printf("curvaturePercent = 100%%, 90-degree corner (handle should land exactly on V):\n");
        Vec2 A{0, 10}, B{0, 0}, C{10, 0};
        CornerParams p;
        p.radius = 4.0;
        p.curvaturePercent = 1.0;
        CornerResult r = ComputeCorner(A, B, C, p);
        Check(r.valid, "corner computed");
        if (r.valid) {
            double D = 4.0 / std::tan(M_PI / 4.0 / 1.0); // theta=90deg -> tan(45deg)=1 -> D=radius
            CheckNear(Dist(r.curve.p0, B), D, 1e-9, "anchor P1 at distance D from vertex");
            CheckNear(Dist(r.curve.p3, B), D, 1e-9, "anchor P2 at distance D from vertex");
            CheckNear(Dist(r.curve.p1, B), 0.0, 1e-9, "handle H1 lands exactly on V at 100%");
            CheckNear(Dist(r.curve.p2, B), 0.0, 1e-9, "handle H2 lands exactly on V at 100%");
        }
    }

    // --- Case 2: curvaturePercent = 0.0 (0%) degenerates to a straight
    // chord between the two tangent points (a beveled/chamfered corner):
    // control points collapse onto the tangent points themselves.
    {
        std::printf("\ncurvaturePercent = 0%%, 90-degree corner (should be a straight chord):\n");
        Vec2 A{0, 10}, B{0, 0}, C{10, 0};
        CornerParams p;
        p.radius = 4.0;
        p.curvaturePercent = 0.0;
        CornerResult r = ComputeCorner(A, B, C, p);
        Check(r.valid, "corner computed");
        if (r.valid) {
            CheckNear(Dist(r.curve.p1, r.curve.p0), 0.0, 1e-9, "handle 1 collapses to tangent point at 0%");
            CheckNear(Dist(r.curve.p2, r.curve.p3), 0.0, 1e-9, "handle 2 collapses to tangent point at 0%");
            // Sample the midpoint: for a degenerate (straight-chord) cubic
            // it should lie exactly on the line between p0 and p3.
            double t = 0.5;
            Vec2 mid = r.curve.p0 * std::pow(1 - t, 3)
                     + r.curve.p1 * (3 * t * std::pow(1 - t, 2))
                     + r.curve.p2 * (3 * t * t * (1 - t))
                     + r.curve.p3 * (t * t * t);
            Vec2 expectedMid = (r.curve.p0 + r.curve.p3) * 0.5;
            CheckNear(Dist(mid, expectedMid), 0.0, 1e-9, "midpoint lies on the straight chord");
        }
    }

    // --- Case 3: handle length is EXACTLY L = D * curvaturePercent (the
    // whole point of the Glyphs formula being a direct proportion, not an
    // interpolation toward some other reference value).
    {
        std::printf("\ncurvaturePercent sweep: handle length == D * curvaturePercent, exactly:\n");
        Vec2 A{0, 10}, B{0, 0}, C{10, 0};
        CornerParams p;
        p.radius = 4.0;
        double D = 4.0 / std::tan(M_PI / 4.0); // theta=90deg -> D == radius == 4.0
        double prevHandle = -1.0;
        bool handleMonotonic = true;
        for (double pct : {0.0, 0.25, 0.5, 0.65, 0.75, 1.0}) {
            p.curvaturePercent = pct;
            CornerResult r = ComputeCorner(A, B, C, p);
            if (!r.valid) { handleMonotonic = false; continue; }
            double handle = Dist(r.curve.p1, r.curve.p0);
            CheckNear(handle, pct * D, 1e-9, "handle length == pct * D exactly");
            if (handle < prevHandle - 1e-9) handleMonotonic = false;
            prevHandle = handle;
        }
        Check(handleMonotonic, "handle length increases monotonically with curvaturePercent");

        // Default (65%, Glyphs' own default) should sit strictly between
        // the bevel and the handle-touches-V extreme.
        p.curvaturePercent = 0.65;
        CornerResult r65 = ComputeCorner(A, B, C, p);
        Check(r65.valid, "default 65%% is valid");
        if (r65.valid) {
            double handle65 = Dist(r65.curve.p1, r65.curve.p0);
            CheckNear(handle65, 0.65 * D, 1e-9, "default 65%% handle == 0.65 * D exactly");
        }
    }

    // --- Case 4: curvature never changes sign within the segment, across
    // a range of angles and percentages.
    //
    // A single cubic Bezier's curvature naturally isn't constant -- at low
    // curvaturePercent it's small near the tangent points (matching the
    // straight edges it joins, G1-ish) and peaks toward the middle; at
    // high percent approximating a large arc sweep it can show a mild dip
    // at the exact middle instead (checked by hand: a 20-degree corner at
    // 100% ranges ~0.094 to ~0.122, dipping to ~0.106 in the center).
    // Both are legitimate, expected shapes for a single-segment fillet.
    // What would actually indicate a broken/self-intersecting curve is
    // curvature CHANGING SIGN partway through -- an inflection point,
    // meaning the curve bends the other way somewhere in the middle (an
    // S-shape) instead of bulging consistently toward the cut corner.
    {
        std::printf("\nCurvature never changes sign (no inflection / S-curve) across angle/percent combinations:\n");
        bool allClean = true;
        for (double angleDeg : {20.0, 45.0, 90.0, 135.0, 160.0}) {
            double interior = angleDeg * M_PI / 180.0;
            Vec2 B{0, 0}, A{-100, 0};
            double v2ang = M_PI - interior;
            Vec2 C{100 * std::cos(v2ang), 100 * std::sin(v2ang)};
            for (double pct : {0.0, 0.1, 0.25, 0.5, 0.65, 1.0}) {
                CornerParams p;
                p.radius = 10.0;
                p.curvaturePercent = pct;
                CornerResult r = ComputeCorner(A, B, C, p);
                if (!r.valid) continue;
                const int N = 200;
                bool sawPositive = false, sawNegative = false;
                for (int i = 5; i <= N - 5; ++i) { // skip the very ends, which can be ~0 by construction
                    double k = CurvatureAt(r.curve, double(i) / N);
                    if (k > 1e-6) sawPositive = true;
                    if (k < -1e-6) sawNegative = true;
                }
                if (sawPositive && sawNegative) {
                    std::printf("  [FAIL] sign change (inflection): angle=%.0f pct=%.2f\n", angleDeg, pct);
                    allClean = false;
                }
            }
        }
        Check(allClean, "curvature stays single-signed (no inflection point) across all tested angle/percent combinations");
    }

    // --- Case 5: degenerate / near-collinear input should be left alone.
    {
        std::printf("\nDegenerate input handling:\n");
        Vec2 A{0, 0}, B{5, 0}, C{10, 0}; // straight line
        CornerParams p; p.radius = 2.0; p.curvaturePercent = 0.65;
        CornerResult r = ComputeCorner(A, B, C, p);
        Check(!r.valid, "near-straight vertex is left untouched");
    }

    // --- Case 6: explicit collinearity check, the exact property the
    // Glyphs formula is spec'd to guarantee -- P1,H1 must lie exactly on
    // the line through V (and likewise P2,H2), across several angles and
    // curvature values, not just "look right" on a 90-degree example.
    {
        std::printf("\nP1,H1 (and P2,H2) collinear with V, across angles/curvatures:\n");
        bool allCollinear = true;
        for (double angleDeg : {30.0, 60.0, 90.0, 120.0, 150.0}) {
            double interior = angleDeg * M_PI / 180.0;
            Vec2 B{0, 0}, A{-100, 0};
            double v2ang = M_PI - interior;
            Vec2 C{100 * std::cos(v2ang), 100 * std::sin(v2ang)};
            for (double pct : {0.1, 0.3, 0.65, 0.9, 1.0}) {
                CornerParams p;
                p.radius = 5.0;
                p.curvaturePercent = pct;
                CornerResult r = ComputeCorner(A, B, C, p);
                if (!r.valid) continue;
                // Collinear with V means (H-V) x (P-V) == 0 (2D cross product).
                double cross1 = (r.curve.p1 - B).cross(r.curve.p0 - B);
                double cross2 = (r.curve.p2 - B).cross(r.curve.p3 - B);
                if (std::fabs(cross1) > 1e-6 || std::fabs(cross2) > 1e-6) {
                    std::printf("  [FAIL] not collinear: angle=%.0f pct=%.2f cross1=%.9f cross2=%.9f\n",
                                angleDeg, pct, cross1, cross2);
                    allCollinear = false;
                }
            }
        }
        Check(allCollinear, "P1/H1 and P2/H2 exactly collinear with V across all tested cases");
    }

    std::printf("\n%s\n", g_failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return g_failures == 0 ? 0 : 1;
}

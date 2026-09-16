// Generates SVG path data comparing Curvature settings (0% / 65% default /
// 100%) on a 5-point star (sharp tips + reflex inner corners -- a good
// stress test), using the Glyphs-style proportional formula: handle
// length L = D * curvaturePercent, D = radius / tan(theta/2). Unlike the
// earlier vector-interpolation algorithm, 100% is NOT an exact circle
// here -- it puts the handle exactly on the vertex V.
// Build:
//   clang++ -std=c++17 -O0 CornerMath.cpp CornerMathDemo.cpp -o /tmp/corner_demo
//   /tmp/corner_demo > /tmp/corner_demo.txt
#include "CornerMath.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace smoothie;

static std::vector<Vec2> MakeStar(Vec2 center, double outerR, double innerR, int points) {
    std::vector<Vec2> pts;
    int n = points * 2;
    for (int i = 0; i < n; ++i) {
        double r = (i % 2 == 0) ? outerR : innerR;
        double angle = -M_PI / 2.0 + i * M_PI / points;
        pts.push_back({center.x + r * std::cos(angle), center.y + r * std::sin(angle)});
    }
    return pts;
}

static void EmitPathD(const std::vector<Vec2>& poly, double radius, double curvaturePercent) {
    size_t n = poly.size();
    CornerParams p;
    p.radius = radius;
    p.curvaturePercent = curvaturePercent;

    std::vector<CornerResult> results(n);
    for (size_t i = 0; i < n; ++i) {
        const Vec2& A = poly[(i + n - 1) % n];
        const Vec2& B = poly[i];
        const Vec2& C = poly[(i + 1) % n];
        results[i] = ComputeCorner(A, B, C, p);
    }

    std::string d;
    char buf[128];

    Vec2 startPt = results[0].valid ? results[0].trimEnd : poly[0];
    std::snprintf(buf, sizeof(buf), "M %.3f %.3f ", startPt.x, startPt.y);
    d += buf;

    for (size_t i = 0; i < n; ++i) {
        size_t next = (i + 1) % n;
        Vec2 lineEnd = results[next].valid ? results[next].trimStart : poly[next];
        std::snprintf(buf, sizeof(buf), "L %.3f %.3f ", lineEnd.x, lineEnd.y);
        d += buf;

        if (results[next].valid) {
            const BezierSeg& seg = results[next].curve;
            std::snprintf(buf, sizeof(buf), "C %.3f %.3f %.3f %.3f %.3f %.3f ",
                          seg.p1.x, seg.p1.y, seg.p2.x, seg.p2.y, seg.p3.x, seg.p3.y);
            d += buf;
        }
    }
    d += "Z";
    std::printf("%s\n", d.c_str());
}

int main() {
    std::vector<Vec2> star = MakeStar({150, 150}, 120, 46, 5);

    std::printf("BEVEL_0PCT\n");
    EmitPathD(star, 14.0, 0.0);

    std::printf("DEFAULT_65PCT\n");
    EmitPathD(star, 14.0, 0.65);

    std::printf("TOVERTEX_100PCT\n");
    EmitPathD(star, 14.0, 1.0);

    // Curvature profile across the tip corner (vertex 0, an acute ~36deg
    // star point) at 0% / 65% / 100%, at a radius small enough to avoid
    // the half-edge safety clamp. Printed as "t,k0,k65,k100" so the shape
    // can be charted.
    {
        size_t n = star.size();
        const Vec2& A = star[n - 1];
        const Vec2& B = star[0];
        const Vec2& C = star[1];

        CornerParams p0; p0.radius = 10.0; p0.curvaturePercent = 0.0;
        CornerParams p65; p65.radius = 10.0; p65.curvaturePercent = 0.65;
        CornerParams p100; p100.radius = 10.0; p100.curvaturePercent = 1.0;

        CornerResult r0 = ComputeCorner(A, B, C, p0);
        CornerResult r65 = ComputeCorner(A, B, C, p65);
        CornerResult r100 = ComputeCorner(A, B, C, p100);

        std::printf("CURVATURE\n");
        const int N = 40;
        for (int i = 0; i <= N; ++i) {
            double t = double(i) / N;
            double k0 = r0.valid ? std::fabs(CurvatureAt(r0.curve, t)) : 0.0;
            double k65 = r65.valid ? std::fabs(CurvatureAt(r65.curve, t)) : 0.0;
            double k100 = r100.valid ? std::fabs(CurvatureAt(r100.curve, t)) : 0.0;
            std::printf("%.4f,%.6f,%.6f,%.6f\n", t, k0, k65, k100);
        }
    }

    return 0;
}

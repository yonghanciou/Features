// KakuMathTest.cpp
// Standalone sanity test for KakuMath, no Illustrator SDK needed.
// Build/run: clang++ -std=c++17 KakuMath.cpp KakuMathTest.cpp -o /tmp/gqmtest && /tmp/gqmtest

#include "KakuMath.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <vector>

using namespace kaku;

static int g_failures = 0;

static void Check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++g_failures; }
    else std::printf("ok:   %s\n", what);
}

// An open L-shaped polyline (not closed) to check the open-path endpoint
// handling in ResamplePath (must always include the final anchor exactly).
static void TestOpenPolyline() {
    Vec2 a{0, 0}, b{50, 0}, c{50, 50};
    std::vector<BezierSeg> segs = { {a, a, b, b}, {b, b, c, c} };
    bool cap = false;
    auto raw = ResamplePath(segs, /*closed=*/false, /*spacing=*/5.0, 15000, &cap);
    Check(!raw.empty(), "open polyline: produces points");
    Check(raw.back().x == 50.0 && raw.back().y == 50.0, "open polyline: last sample is exactly the final anchor (50,50)");
}

// A circle approximated by 4 cubic Bezier quadrants (kappa constant),
// checked against the analytic area of its grid-snapped polygon staying
// within a sane tolerance of pi*r^2 -- catches gross sign/axis mistakes in
// BezierAt without needing exact pixel matching.
static void TestCircleResampleArea() {
    const double r = 50.0, kappa = 0.5522847498;
    Vec2 c0{r, 0}, c1{r, r * kappa}, c2{r * kappa, r}, c3{0, r};
    Vec2 c4{-r * kappa, r}, c5{-r, r * kappa}, c6{-r, 0};
    Vec2 c7{-r, -r * kappa}, c8{-r * kappa, -r}, c9{0, -r};
    Vec2 c10{r * kappa, -r}, c11{r, -r * kappa};
    std::vector<BezierSeg> segs = {
        {c0, c1, c2, c3}, {c3, c4, c5, c6}, {c6, c7, c8, c9}, {c9, c10, c11, c0}
    };
    bool cap = false;
    auto raw = ResamplePath(segs, true, 2.0, 15000, &cap);
    Check(raw.size() > 100, "circle resample: reasonably dense polyline at spacing=2 on r=50 circle");

    double area2 = 0.0; // shoelace * 2
    for (size_t i = 0; i < raw.size(); ++i) {
        const Vec2& p1 = raw[i];
        const Vec2& p2 = raw[(i + 1) % raw.size()];
        area2 += p1.x * p2.y - p2.x * p1.y;
    }
    double area = std::fabs(area2) / 2.0;
    double expected = M_PI * r * r;
    Check(std::fabs(area - expected) / expected < 0.01, "circle resample: polygon area within 1% of pi*r^2");
}

// ===========================================================================
// Phase 2: RasterizeToContours / BoundingBoxCells / DeriveEffectiveCell.
// Same shapes and expected numbers already validated standalone in Node
// against the jsx's algorithm (see the grid-quantize.jsx conversation) --
// ported here so the C++ port is checked against the exact same references.
// ===========================================================================

static double PolygonArea(const std::vector<Vec2>& pts) {
    double a = 0.0;
    size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        const Vec2& p1 = pts[i];
        const Vec2& p2 = pts[(i + 1) % n];
        a += p1.x * p2.y - p2.x * p1.y;
    }
    return std::fabs(a) / 2.0;
}

static std::vector<Vec2> CirclePolygon(double r, double cx, double cy, int n) {
    std::vector<Vec2> pts;
    pts.reserve(n);
    for (int i = 0; i < n; ++i) {
        double a = (double)i / n * 2.0 * M_PI;
        pts.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
    }
    return pts;
}

static void TestRasterizeCircle() {
    auto circle = CirclePolygon(50.0, 0.0, 0.0, 200);
    auto res = RasterizeToContours(circle, 4.0, -60.0, 60.0, true);
    Check(!res.tooBig, "rasterize circle: not flagged too-big");
    Check(res.contours.size() == 1, "rasterize circle: exactly 1 contour for a solid circle");
    if (res.contours.size() == 1) {
        double area = PolygonArea(res.contours[0]);
        double expected = M_PI * 50.0 * 50.0;
        // Conservative (touches-the-cell) fill is deliberately a bit larger
        // than the analytic circle area, not smaller -- verified in the JS
        // prototype (~9% over at this cell size). Reject only if it's
        // *smaller* (a regression back toward the old point-sampling bug)
        // or wildly oversized (a real bug).
        Check(area >= expected && area <= expected * 1.20,
              "rasterize circle: area is >= analytic pi*r^2 and within 20% over (conservative fill, not a regression)");
    }
}

// A real dumbbell polygon (single simple non-self-intersecting outline)
// with a very thin (1pt) neck; at cell=10 the neck should vanish and the
// shape should split into two separate square blobs from ONE input polygon.
static std::vector<Vec2> DumbbellPolygon() {
    return {
        {-60, 25}, {-20, 25}, {-20, 0.5}, {20, 0.5}, {20, 25}, {60, 25},
        {60, -25}, {20, -25}, {20, -0.5}, {-20, -0.5}, {-20, -25}, {-60, -25}
    };
}

static void TestRasterizeDumbbellSplits() {
    auto res = RasterizeToContours(DumbbellPolygon(), 10.0, -70.0, 30.0, true);
    Check(!res.tooBig, "rasterize dumbbell: not flagged too-big");
    Check(res.contours.size() == 2, "rasterize dumbbell: thin neck vanishes into 2 separate blobs");
    if (res.contours.size() == 2) {
        Check(res.contours[0].size() == 4 && res.contours[1].size() == 4,
              "rasterize dumbbell: each blob simplifies to a plain 4-point rectangle");
        Check(PolygonArea(res.contours[0]) == 2400.0 && PolygonArea(res.contours[1]) == 2400.0,
              "rasterize dumbbell: each blob is exactly 2400 sq pt (40x60)");
    }
}

// A thin sinuous ribbon (like a cursive glyph's stroke) -- this is the exact
// shape of bug report that motivated switching from center-point sampling
// to the conservative+3-subscan rule: certain cell sizes used to produce
// "0 條路徑" (RasterizeToContours returning zero contours) here.
static std::vector<Vec2> ThinSCurvePolygon(double halfWidth, int samples) {
    auto centerAt = [](double t) -> Vec2 {
        double y = (t - 0.5) * 200.0;
        double x = 40.0 * std::sin(t * M_PI * 2.0);
        return {x, y};
    };
    std::vector<Vec2> left, right;
    for (int i = 0; i <= samples; ++i) {
        double t = (double)i / samples;
        double eps = 0.001;
        Vec2 p0 = centerAt(std::max(0.0, t - eps));
        Vec2 p1 = centerAt(std::min(1.0, t + eps));
        double dx = p1.x - p0.x, dy = p1.y - p0.y;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-9) len = 1.0;
        double nx = -dy / len, ny = dx / len;
        Vec2 c = centerAt(t);
        left.push_back({c.x + nx * halfWidth, c.y + ny * halfWidth});
        right.push_back({c.x - nx * halfWidth, c.y - ny * halfWidth});
    }
    std::reverse(right.begin(), right.end());
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

static void TestRasterizeThinStrokeNoLongerVanishes() {
    auto stroke = ThinSCurvePolygon(10.0, 200);
    double failingCells[] = {44.08, 29.13, 28.38};
    for (double cell : failingCells) {
        auto res = RasterizeToContours(stroke, cell, -60.0, 110.0, true);
        char label[128];
        std::snprintf(label, sizeof(label), "rasterize thin S-stroke at cell=%.2f: no longer 0 contours", cell);
        Check(!res.contours.empty(), label);
    }
}

static void TestBoundingBoxCellsCollapse() {
    // The exact real-world failure case: a small object (10x8pt) with a
    // cell (45pt) bigger than the object itself -- should collapse to a
    // single 45x45 rectangle, not an empty result.
    std::vector<Vec2> pts = {{0, 0}, {10, 0}, {10, -8}, {0, -8}};
    auto rect = BoundingBoxCells(pts, 45.0, 0.0, 0.0);
    Check(rect.size() == 4, "bounding box collapse: returns exactly 4 corners");
    if (rect.size() == 4) {
        double w = std::fabs(rect[1].x - rect[0].x);
        double h = std::fabs(rect[0].y - rect[2].y);
        Check(w == 45.0 && h == 45.0, "bounding box collapse: exactly one 45x45 cell, not zero-sized");
    }
}

static void TestEffectiveCellFromRatio() {
    Check(EffectiveCellFromRatio(2.0, 200.0, 100.0) == 100.0, "effective cell: ratio=2 over 200x100 bounds -> 100pt cells");
    Check(EffectiveCellFromRatio(1.0, 200.0, 100.0) == 200.0, "effective cell: ratio=1 (most extreme) -> 200pt, whole shape is one block");
    Check(EffectiveCellFromRatio(8.0, 0.0, 0.0) == 1.0, "effective cell: degenerate zero bounds falls back to 1.0");
}

// Reproduces the real bug report: a compound path with a hole (a donut, or
// a letterform like "P" whose counter is a separate sub-path) must keep
// its hole after 完全像素化. A square outer contour + a smaller square
// counter, drawn with standard compound-path winding (outer one direction,
// hole the opposite), is the minimal repro for "P"'s bowl.
static std::vector<Vec2> SquareCW(double half, double cx, double cy) {
    // clockwise in this y-up convention: (cx-h,cy-h) -> (cx-h,cy+h) -> (cx+h,cy+h) -> (cx+h,cy-h)
    return { {cx-half, cy-half}, {cx-half, cy+half}, {cx+half, cy+half}, {cx+half, cy-half} };
}
static std::vector<Vec2> SquareCCW(double half, double cx, double cy) {
    return { {cx-half, cy-half}, {cx+half, cy-half}, {cx+half, cy+half}, {cx-half, cy+half} };
}

static void TestCompoundHoleSurvivesPixelation() {
    auto outer = SquareCW(50, 0, 0);   // 100x100 outer square
    auto hole = SquareCCW(20, 0, 0);   // 40x40 counter, opposite winding -> a hole

    // OLD (broken) behavior: rasterize each sub-path independently. This
    // must reproduce the bug (both come out solid, hole lost) so the "new"
    // test below is proven to actually be fixing something, not just
    // trivially passing.
    auto outerAlone = RasterizeToContours(outer, 5.0, -60.0, 60.0, true);
    auto holeAlone = RasterizeToContours(hole, 5.0, -60.0, 60.0, true);
    Check(outerAlone.contours.size() == 1 && holeAlone.contours.size() == 1,
          "compound hole repro: each sub-path alone rasterizes to exactly 1 solid contour (confirms the bug's precondition)");

    // NEW (fixed) behavior: rasterize both sub-paths together with
    // nonzero-winding fill.
    auto combined = RasterizeCompoundToContours({outer, hole}, 5.0, -60.0, 60.0, true);
    Check(!combined.tooBig, "compound hole fix: not flagged too-big");
    Check(combined.contours.size() == 2, "compound hole fix: produces 2 contours (outer boundary + hole boundary), not 1 solid blob");
    if (combined.contours.size() == 2) {
        double a0 = PolygonArea(combined.contours[0]);
        double a1 = PolygonArea(combined.contours[1]);
        double outerArea = std::max(a0, a1);
        double holeArea = std::min(a0, a1);
        Check(outerArea > 8000.0 && outerArea < 10500.0, "compound hole fix: outer contour area is close to the 100x100 square (10000)");
        Check(holeArea > 1200.0 && holeArea < 2200.0, "compound hole fix: hole contour area is close to the 40x40 counter (1600)");
    }
}

// Reproduces the real bug report: 完全像素化 on "Lorem ipsum" showed spurious
// diagonal notches at serif corner junctions. Minimal repro: a "bowtie"
// polygon (two right triangles meeting at a single point, like a serif
// corner can produce after rasterizing) -- at the right cell alignment,
// each triangle rasterizes to its own single cell, and those two cells
// touch only diagonally. Before the ResolveDiagonalTouches fix this could
// corrupt the traced loop (dropped/misrouted edges via the overwritten map
// entry); the meaningful checks here are that it doesn't corrupt (bounded
// point counts, no runaway loop -- the guardMax cap would otherwise let a
// corrupted trace run long but not infinite, so a sane point count is the
// signal) and that the total filled area is still close to both triangles'
// combined area (no area silently lost to a bad cut).
static std::vector<Vec2> BowtiePolygon() {
    return {
        {10, 0}, {0, 10}, {0, 0},   // triangle 1 (upper-right of origin)
        {-10, 0}, {0, -10}, {0, 0}  // triangle 2 (lower-left of origin), touching triangle 1 only at (0,0)
    };
}

static void TestDiagonalTouchDoesNotCorruptBoundary() {
    auto bowtie = BowtiePolygon();
    auto res = RasterizeToContours(bowtie, 10.0, -10.0, 10.0, true);
    Check(!res.tooBig, "diagonal touch: not flagged too-big");
    Check(!res.contours.empty(), "diagonal touch: produces at least one contour, not corrupted into nothing");

    double totalArea = 0.0;
    bool allSane = true;
    for (auto& c : res.contours) {
        if (c.size() < 3 || c.size() > 50) allSane = false; // a corrupted trace tends to blow up point count
        totalArea += PolygonArea(c);
    }
    Check(allSane, "diagonal touch: every contour has a sane point count (no corrupted runaway loop)");
    // Each triangle occupies roughly one 10x10 cell once conservatively
    // rasterized; two touching/bridged cells -> around 200 sq units. Wide
    // tolerance since the exact bridging choice affects the shape a bit.
    Check(totalArea > 60.0 && totalArea < 400.0, "diagonal touch: total filled area is in a sane range (no area silently lost or exploded)");
}

// Reproduces the real bug report: 像素化 on a rounded square showed small
// specks/notches along its perfectly straight sides. Root cause: a
// straight edge's x-coordinate sitting almost exactly on a cell boundary,
// sampled at many slightly-different points, can pick up ULP-level
// floating-point jitter that flips floor()/ceil() by one cell for isolated
// rows -- popping a spurious single-cell bump out of (or into) an
// otherwise constant column.
//
// Simulated as a sequence of purely vertical one-row-tall segments (not a
// smooth zigzag -- interpolation across a zigzag blends consecutive
// jittered vertices together and washes the effect out, verified by
// actually trying that first) so each row's scanline crossing lands on
// exactly one segment's fixed, deliberately jittered x with no blending --
// isolating the one thing being tested: does a lone row's boundary
// computation get the same column as its neighbors despite ULP-level
// jitter, with the true x placed exactly on a cell boundary (the worst
// case for floor/ceil flip-flopping).
static std::vector<Vec2> SquareWaveJitteredRectangle(double left, double right, double bottom, double top,
                                                      double cell, double jitter) {
    std::vector<Vec2> pts;
    int nRows = (int)((top - bottom) / cell);
    pts.push_back({right, bottom});
    pts.push_back({right, top}); // right edge: clean control side
    for (int r = 0; r < nRows; ++r) {
        double y0 = top - r * cell;
        double y1 = top - (r + 1) * cell;
        double j = (r % 2 == 0) ? jitter : -jitter; // alternates sign every row
        pts.push_back({left + j, y0});
        pts.push_back({left + j, y1});
    }
    return pts;
}

static void TestBoundaryEpsilonAbsorbsFloatingPointJitter() {
    // cell=10, left edge exactly at x=100 (10 * cell) -- sits exactly on a
    // cell boundary. Verified this exact setup WITHOUT the epsilon fix
    // reproduces the bug precisely: a 42-point staircase alternating
    // between x=90 and x=100 every row, instead of a clean rectangle.
    auto rect = SquareWaveJitteredRectangle(100.0, 150.0, 0.0, 200.0, 10.0, 1e-9);
    auto res = RasterizeToContours(rect, 10.0, 0.0, 200.0, true);
    Check(!res.tooBig, "boundary epsilon: not flagged too-big");
    Check(res.contours.size() == 1, "boundary epsilon: exactly 1 contour for a clean rectangle");
    if (res.contours.size() == 1) {
        Check(res.contours[0].size() == 4,
              "boundary epsilon: simplifies to exactly 4 corners despite alternating per-row left-edge jitter (no staircase)");
    }
}

// ===========================================================================
// 多邊形 (PolygonizeSegments): port of Polygonize.jsx's polygonize(). Not
// grid-based -- verifies straight segments are left untouched (no spurious
// subdivision) while curved segments get exactly facets-1 extra points, and
// that closed/open segment-count handling matches ResamplePath's convention
// (both rely on the same "segs.size() already accounts for closed/open"
// contract from ReadPathAsBeziers).
// ===========================================================================

// A closed square as 4 perfectly straight segments (handles collapsed onto
// their anchors, same as how a corner-only Illustrator path represents a
// straight edge).
static std::vector<BezierSeg> SquareSegsClosed() {
    Vec2 a{0, 0}, b{100, 0}, c{100, 100}, d{0, 100};
    return {
        {a, a, b, b},
        {b, b, c, c},
        {c, c, d, d},
        {d, d, a, a},
    };
}

static void TestPolygonizeLeavesStraightSegmentsAlone() {
    auto segs = SquareSegsClosed();
    auto out = PolygonizeSegments(segs, true, 6); // facets=6 -- would matter a lot if segments were (wrongly) treated as curved
    Check(out.size() == 4, "polygonize straight: 4 perfectly straight segments stay exactly 4 points regardless of facets");
    if (out.size() == 4) {
        Check(out[0].x == 0 && out[0].y == 0 && out[1].x == 100 && out[1].y == 0 &&
              out[2].x == 100 && out[2].y == 100 && out[3].x == 0 && out[3].y == 100,
              "polygonize straight: corners are exactly the original 4 anchors, unmoved");
    }
}

// A single circular quadrant (genuinely curved, kappa-constant handles) --
// checks the point count matches Polygonize.jsx's exact formula (anchor +
// facets-1 interior points per curved segment) and that the interior points
// actually lie on the curve (not on the straight chord), i.e. subdivision
// really happened.
static void TestPolygonizeSubdividesCurvedSegments() {
    const double r = 50.0, kappa = 0.5522847498;
    Vec2 p0{r, 0}, p1{r, r * kappa}, p2{r * kappa, r}, p3{0, r};
    std::vector<BezierSeg> segs = { {p0, p1, p2, p3} }; // one curved segment, open path

    for (int facets = 1; facets <= 6; ++facets) {
        auto out = PolygonizeSegments(segs, /*closed=*/false, facets);
        // open path: 1 segment contributes [p0, facets-1 interior points], then the
        // final anchor p3 is appended once at the end (mirrors ResamplePath's
        // open-path tail-append) -> total = 1 + (facets-1) + 1 = facets + 1.
        size_t expected = (size_t)facets + 1;
        char label[128];
        std::snprintf(label, sizeof(label), "polygonize curved: facets=%d produces exactly %zu points", facets, expected);
        Check(out.size() == expected, label);
    }

    auto out3 = PolygonizeSegments(segs, false, 4);
    if (out3.size() == 5) {
        // Middle interior point (f=2/4=0.5) should sit ON the bezier curve,
        // not on the straight p0-p3 chord -- confirms real subdivision, not
        // just linear interpolation between anchors.
        Vec2 onCurve = BezierAt(segs[0], 0.5);
        Vec2 onChord = { (p0.x + p3.x) / 2.0, (p0.y + p3.y) / 2.0 };
        double distToCurve = std::fabs(out3[2].x - onCurve.x) + std::fabs(out3[2].y - onCurve.y);
        double distToChord = std::fabs(out3[2].x - onChord.x) + std::fabs(out3[2].y - onChord.y);
        Check(distToCurve < 1e-9 && distToChord > 1.0,
              "polygonize curved: interior sample lies on the actual curve, not the straight chord");
    }
}

// A closed shape mixing one straight edge with one curved edge -- checks
// the two segment types are handled independently within the same path
// (the straight edge contributing no extra points, the curved edge
// contributing facets-1).
static void TestPolygonizeMixedStraightAndCurved() {
    const double r = 50.0, kappa = 0.5522847498;
    Vec2 a{0, 0}, b{100, 0};                                  // straight edge a->b
    Vec2 c1{100 + r * kappa, 0}, c2{100 + r, r * kappa}, c{100 + r, r}; // curved edge b->c (quarter arc)
    Vec2 d{0, r};                                             // straight edges c->d and d->a
    std::vector<BezierSeg> segs = {
        {a, a, b, b},           // straight
        {b, c1, c2, c},         // curved
        {c, c, d, d},           // straight
        {d, d, a, a},           // straight
    };
    int facets = 5;
    auto out = PolygonizeSegments(segs, true, facets);
    // 3 straight segments contribute 1 point each (their start anchor);
    // 1 curved segment contributes 1 + (facets-1) = facets points.
    size_t expected = 3 + (size_t)facets;
    Check(out.size() == expected, "polygonize mixed: straight edges contribute 1 point each, the curved edge contributes facets points");
}

int main() {
    TestOpenPolyline();
    TestCircleResampleArea();
    TestRasterizeCircle();
    TestRasterizeDumbbellSplits();
    TestRasterizeThinStrokeNoLongerVanishes();
    TestBoundingBoxCellsCollapse();
    TestEffectiveCellFromRatio();
    TestCompoundHoleSurvivesPixelation();
    TestDiagonalTouchDoesNotCorruptBoundary();
    TestBoundaryEpsilonAbsorbsFloatingPointJitter();
    TestPolygonizeLeavesStraightSegmentsAlone();
    TestPolygonizeSubdividesCurvedSegments();
    TestPolygonizeMixedStraightAndCurved();
    std::printf("\n%s\n", g_failures == 0 ? "ALL PASS" : "SOME FAILED");
    return g_failures == 0 ? 0 : 1;
}

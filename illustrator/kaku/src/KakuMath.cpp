// KakuMath.cpp
// See KakuMath.h. Direct port of resample()/bezierAt()/segLength()/
// snapAll()/simplifyCollinear() from grid-quantize.jsx -- same formulas,
// same step/point caps, same collinearity epsilon.

#include "KakuMath.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace kaku {

Vec2 BezierAt(const BezierSeg& seg, double t) {
    double mt = 1.0 - t;
    double a = mt * mt * mt;
    double b = 3.0 * mt * mt * t;
    double c = 3.0 * mt * t * t;
    double d = t * t * t;
    return seg.p0 * a + seg.p1 * b + seg.p2 * c + seg.p3 * d;
}

double SegLength(const BezierSeg& seg) {
    const int n = 12;
    double len = 0.0;
    Vec2 prev = seg.p0;
    for (int i = 1; i <= n; ++i) {
        Vec2 cur = BezierAt(seg, (double)i / n);
        double dx = cur.x - prev.x, dy = cur.y - prev.y;
        len += std::sqrt(dx * dx + dy * dy);
        prev = cur;
    }
    return len;
}

std::vector<Vec2> ResamplePath(const std::vector<BezierSeg>& segs, bool closed,
                                double spacing, size_t maxPts, bool* hitCap) {
    std::vector<Vec2> out;
    bool cap = false;
    if (spacing < 1e-9) spacing = 1e-9;

    size_t last = closed ? segs.size() : (segs.size() > 0 ? segs.size() - 1 : 0);
    for (size_t i = 0; i < last && i < segs.size(); ++i) {
        const BezierSeg& seg = segs[i % segs.size()];
        double len = SegLength(seg);
        int steps = (int)std::ceil(len / spacing);
        if (steps < 1) steps = 1;
        if (steps > 600) { steps = 600; cap = true; }

        for (int s = 0; s < steps; ++s) {
            out.push_back(BezierAt(seg, (double)s / steps));
        }
        if (out.size() > maxPts) { cap = true; break; }
    }

    if (!closed && !segs.empty()) out.push_back(segs.back().p3);
    if (hitCap) *hitCap = cap;
    return out;
}

std::vector<Vec2> SnapToGrid(const std::vector<Vec2>& pts, double cell, double gx, double gy) {
    std::vector<Vec2> out;
    out.reserve(pts.size());
    for (const auto& p : pts) {
        double x = std::round((p.x - gx) / cell) * cell + gx;
        double y = std::round((p.y - gy) / cell) * cell + gy;
        if (out.empty() || out.back().x != x || out.back().y != y) {
            out.push_back({x, y});
        }
    }
    return out;
}

std::vector<Vec2> SimplifyCollinear(const std::vector<Vec2>& pts, bool closed) {
    size_t n = pts.size();
    if (n < 3) return pts;
    std::vector<Vec2> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (!closed && (i == 0 || i == n - 1)) { out.push_back(pts[i]); continue; }
        const Vec2& pv = pts[(i + n - 1) % n];
        const Vec2& cu = pts[i];
        const Vec2& nx = pts[(i + 1) % n];
        double cross = (cu.x - pv.x) * (nx.y - pv.y) - (cu.y - pv.y) * (nx.x - pv.x);
        if (std::fabs(cross) > 1e-6) out.push_back(cu);
    }
    size_t minLen = closed ? 3 : 2;
    return (out.size() >= minLen) ? out : pts;
}

namespace {

// Floating-point robustness: a crossing x-coordinate extremely close to an
// exact cell boundary -- very plausible for shapes with round coordinates
// on a ratio-derived cell size, e.g. a rounded rectangle's straight edge
// sampled at many different Bezier t values, each accumulating slightly
// different floating-point rounding even though the true x is
// mathematically constant along that edge -- can flip floor()/ceil() by
// one cell between rows that should be identical, popping out spurious
// single-cell bumps along an otherwise straight edge (verified against a
// real report: 像素化 mode showed small notches along a rounded square's
// straight sides). Biasing floor/ceil by a tiny epsilon relative to cell
// size makes near-boundary jitter resolve the same way every time.
const double kBoundaryEpsilon = 1e-6;

long CellFloor(double value, double cell) {
    return (long)std::floor(value / cell + kBoundaryEpsilon);
}
long CellCeil(double value, double cell) {
    return (long)std::ceil(value / cell - kBoundaryEpsilon);
}

} // namespace

double EffectiveCellFromRatio(double cellRatio, double boundsW, double boundsH) {
    double cell = 1.0;
    double longSide = std::max(boundsW, boundsH);
    if (std::isfinite(longSide) && longSide > 0.0 && cellRatio > 0.0) {
        cell = longSide / cellRatio;
    }
    if (!std::isfinite(cell) || cell <= 0.0) cell = 1.0;
    return cell;
}

std::vector<Vec2> BoundingBoxCells(const std::vector<Vec2>& pts, double cell, double gx, double gy) {
    if (pts.empty()) return {};
    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;
    for (const auto& p : pts) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }

    double c0 = (double)CellFloor(minX - gx, cell);
    double c1 = (double)CellCeil(maxX - gx, cell);
    double r0 = (double)CellFloor(gy - maxY, cell);
    double r1 = (double)CellCeil(gy - minY, cell);
    if (c1 <= c0) c1 = c0 + 1;
    if (r1 <= r0) r1 = r0 + 1;

    double x0 = gx + c0 * cell, x1 = gx + c1 * cell;
    double y0 = gy - r0 * cell, y1 = gy - r1 * cell;
    return { {x0, y0}, {x1, y0}, {x1, y1}, {x0, y1} };
}

// ---------------------------------------------------------------------
// RasterizeToContours: direct port of grid-quantize.jsx's processPathBlock,
// including the fix (verified against a real failure report: a cursive
// glyph's thin stroke producing "0 條路徑" at certain cell sizes) of using
// 3 sub-scanlines per row and a conservative "touches the cell at all"
// rule instead of pure center-point sampling.
// ---------------------------------------------------------------------

namespace {

struct CellKey {
    long x = 0, y = 0;
    bool operator==(const CellKey& o) const { return x == o.x && y == o.y; }
};

struct CellKeyHash {
    size_t operator()(const CellKey& k) const {
        return std::hash<long>()(k.x) * 1000003u ^ std::hash<long>()(k.y);
    }
};

// Grid bounding box shared by RasterizeToContours and
// RasterizeCompoundToContours: given a combined bbox in document units,
// works out the col/row index range, or flags `tooBig` if it would need
// more than 2M cells.
struct GridBounds {
    long colMin = 0, rowMin = 0, nCols = 0, nRows = 0;
    bool tooBig = false;
    bool empty = false; // no usable bbox at all (e.g. no input points)
};

GridBounds ComputeGridBounds(double minX, double maxX, double minY, double maxY, double cell, double gx, double gy) {
    GridBounds gb;
    if (minX > maxX || minY > maxY) { gb.empty = true; return gb; }

    long colMax = CellCeil(maxX - gx, cell) - 1;
    long rowMax = CellCeil(gy - minY, cell) - 1;
    gb.colMin = CellFloor(minX - gx, cell);
    gb.rowMin = CellFloor(gy - maxY, cell);
    if (colMax < gb.colMin) colMax = gb.colMin;
    if (rowMax < gb.rowMin) rowMax = gb.rowMin;

    gb.nCols = colMax - gb.colMin + 1;
    gb.nRows = rowMax - gb.rowMin + 1;
    if (gb.nCols < 1 || gb.nRows < 1 || (double)gb.nCols * (double)gb.nRows > 2000000.0) gb.tooBig = true;
    return gb;
}

// A cell and its diagonal neighbor both filled, but the two cells that
// would connect them orthogonally both empty, is a classic marching-
// squares ambiguity: the boundary trace below keys its edge map by lattice
// *point*, one outgoing edge per point, so the two lobes meeting at that
// single corner fight over the same map entry -- whichever cell is
// processed last in the (unordered) grid scan silently overwrites the
// other's edge, corrupting the traced loop into a spurious diagonal-
// looking notch instead of a clean right angle. Real-world trigger:
// serif-font corner junctions (verified against a real report -- 完全像素化
// on "Lorem ipsum" showed exactly this at several letter joints).
//
// Fixed upstream rather than during the walk: bridge the ambiguous corner
// by filling one of the two empty connecting cells, so by the time
// TraceGridBoundary runs, no lattice point has more than one legitimate
// outgoing edge to begin with. This also happens to read as the more
// expected "pixel art" behavior (diagonally-touching pixels connect)
// rather than an arbitrary pinch.
void ResolveDiagonalTouches(std::vector<std::vector<bool>>* grid, long nCols, long nRows) {
    for (long rr = 0; rr + 1 < nRows; ++rr) {
        for (long cc = 0; cc + 1 < nCols; ++cc) {
            bool a = (*grid)[(size_t)rr][(size_t)cc];         // top-left
            bool b = (*grid)[(size_t)rr][(size_t)cc + 1];     // top-right
            bool c = (*grid)[(size_t)rr + 1][(size_t)cc];     // bottom-left
            bool d = (*grid)[(size_t)rr + 1][(size_t)cc + 1]; // bottom-right
            if (a && d && !b && !c) {
                (*grid)[(size_t)rr][(size_t)cc + 1] = true; // bridge the "\" touch via top-right
            } else if (b && c && !a && !d) {
                (*grid)[(size_t)rr][(size_t)cc] = true; // bridge the "/" touch via top-left
            }
        }
    }
}

// Boundary trace shared by both rasterize functions: each filled cell emits
// a boundary edge only on the sides where its neighbor is NOT filled,
// walked in a fixed rotational order -- outer boundaries and holes come out
// with opposite winding automatically, no separate hole-detection pass
// needed (this only works because the caller already resolved fill state
// correctly -- nonzero-winding across sub-polygons for
// RasterizeCompoundToContours, plain even-odd for a lone polygon in
// RasterizeToContours -- and because ResolveDiagonalTouches above already
// removed any lattice-point edge ambiguity).
std::vector<std::vector<Vec2>> TraceGridBoundary(std::vector<std::vector<bool>> grid,
                                                  long nCols, long nRows, long colMin, long rowMin,
                                                  double cell, double gx, double gy, bool simplify) {
    ResolveDiagonalTouches(&grid, nCols, nRows);

    auto filledAt = [&](long rr, long cc) -> bool {
        if (rr < 0 || rr >= nRows || cc < 0 || cc >= nCols) return false;
        return grid[(size_t)rr][(size_t)cc];
    };

    std::unordered_map<CellKey, CellKey, CellKeyHash> nextMap;
    for (long rr = 0; rr < nRows; ++rr) {
        for (long cc = 0; cc < nCols; ++cc) {
            if (!grid[(size_t)rr][(size_t)cc]) continue;
            CellKey TL{cc, rr}, TR{cc + 1, rr}, BR{cc + 1, rr + 1}, BL{cc, rr + 1};
            if (!filledAt(rr - 1, cc)) nextMap[TL] = TR;
            if (!filledAt(rr, cc + 1)) nextMap[TR] = BR;
            if (!filledAt(rr + 1, cc)) nextMap[BR] = BL;
            if (!filledAt(rr, cc - 1)) nextMap[BL] = TL;
        }
    }

    auto keyToPt = [&](const CellKey& k) -> Vec2 {
        return { gx + (double)(colMin + k.x) * cell, gy - (double)(rowMin + k.y) * cell };
    };

    std::vector<std::vector<Vec2>> contours;
    std::unordered_map<CellKey, bool, CellKeyHash> consumed;
    consumed.reserve(nextMap.size());

    for (const auto& startEntry : nextMap) {
        const CellKey& startKey = startEntry.first;
        if (consumed.count(startKey)) continue;

        std::vector<Vec2> loop;
        CellKey curKey = startKey;
        size_t guardMax = (size_t)(nCols + 1) * (size_t)(nRows + 1) * 4 + 8;
        size_t guardCount = 0;
        while (guardCount < guardMax) {
            auto it = nextMap.find(curKey);
            if (it == nextMap.end() || consumed.count(curKey)) break;
            loop.push_back(keyToPt(curKey));
            consumed[curKey] = true;
            curKey = it->second;
            ++guardCount;
            if (curKey == startKey) break;
        }
        if (loop.size() >= 3) contours.push_back(std::move(loop));
    }

    if (simplify) {
        for (auto& c : contours) c = SimplifyCollinear(c, true);
    }

    std::vector<std::vector<Vec2>> kept;
    for (auto& c : contours) {
        if (c.size() >= 3) kept.push_back(std::move(c));
    }
    return kept;
}

} // namespace

RasterizeResult RasterizeToContours(const std::vector<Vec2>& polygon, double cell,
                                     double gx, double gy, bool simplify) {
    RasterizeResult result;
    if (polygon.size() < 3 || cell <= 0.0) return result;

    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;
    for (const auto& p : polygon) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }

    GridBounds gb = ComputeGridBounds(minX, maxX, minY, maxY, cell, gx, gy);
    if (gb.empty) return result;
    if (gb.tooBig) { result.tooBig = true; return result; }
    long colMin = gb.colMin, rowMin = gb.rowMin, nCols = gb.nCols, nRows = gb.nRows;

    size_t n = polygon.size();
    std::vector<std::vector<bool>> grid((size_t)nRows, std::vector<bool>((size_t)nCols, false));
    bool anyFilled = false;
    const int kSubscan = 3;

    for (long rr = 0; rr < nRows; ++rr) {
        for (int sub = 0; sub < kSubscan; ++sub) {
            double frac = (sub + 0.5) / kSubscan;
            double yTest = gy - (rowMin + rr + frac) * cell;
            std::vector<double> xs;
            for (size_t e = 0; e < n; ++e) {
                const Vec2& A = polygon[e];
                const Vec2& B = polygon[(e + 1) % n];
                double ay = A.y, by = B.y;
                if (ay == by) continue;
                if ((yTest >= ay && yTest < by) || (yTest >= by && yTest < ay)) {
                    double t = (yTest - ay) / (by - ay);
                    xs.push_back(A.x + t * (B.x - A.x));
                }
            }
            if (xs.size() < 2) continue;
            std::sort(xs.begin(), xs.end());

            for (size_t pi = 0; pi + 1 < xs.size(); pi += 2) {
                long cFrom = CellFloor(xs[pi] - gx, cell) - colMin;
                long cTo = CellCeil(xs[pi + 1] - gx, cell) - 1 - colMin;
                if (cFrom < 0) cFrom = 0;
                if (cTo > nCols - 1) cTo = nCols - 1;
                for (long cc = cFrom; cc <= cTo; ++cc) {
                    grid[(size_t)rr][(size_t)cc] = true;
                    anyFilled = true;
                }
            }
        }
    }

    if (!anyFilled) {
        // Whole object sits inside one or two cells and none of the sample
        // points happened to land inside it: for a small grid, just force-
        // fill it -- this is the "push cell size to the extreme" case, not
        // a bug, so don't just give up (matches the jsx's own fallback).
        if (nCols * nRows <= 4) {
            for (long rr = 0; rr < nRows; ++rr)
                for (long cc = 0; cc < nCols; ++cc) grid[(size_t)rr][(size_t)cc] = true;
            anyFilled = true;
        } else {
            return result; // degenerate: empty contours, not tooBig
        }
    }

    result.contours = TraceGridBoundary(grid, nCols, nRows, colMin, rowMin, cell, gx, gy, simplify);
    return result;
}

// ---------------------------------------------------------------------
// RasterizeCompoundToContours: same grid setup and boundary trace as
// RasterizeToContours, but the fill test is nonzero-winding across ALL
// sub-polygons combined instead of even-odd within one polygon alone --
// see KakuMath.h's comment on this function for why that's
// required for compound-path holes to survive pixelation.
// ---------------------------------------------------------------------
RasterizeResult RasterizeCompoundToContours(const std::vector<std::vector<Vec2>>& subpolygons,
                                             double cell, double gx, double gy, bool simplify) {
    RasterizeResult result;
    if (cell <= 0.0) return result;

    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;
    bool any = false;
    for (const auto& poly : subpolygons) {
        if (poly.size() < 3) continue;
        any = true;
        for (const auto& p : poly) {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }
    }
    if (!any) return result;

    GridBounds gb = ComputeGridBounds(minX, maxX, minY, maxY, cell, gx, gy);
    if (gb.empty) return result;
    if (gb.tooBig) { result.tooBig = true; return result; }
    long colMin = gb.colMin, rowMin = gb.rowMin, nCols = gb.nCols, nRows = gb.nRows;

    std::vector<std::vector<bool>> grid((size_t)nRows, std::vector<bool>((size_t)nCols, false));
    bool anyFilled = false;
    const int kSubscan = 3;

    for (long rr = 0; rr < nRows; ++rr) {
        for (int sub = 0; sub < kSubscan; ++sub) {
            double frac = (sub + 0.5) / kSubscan;
            double yTest = gy - (rowMin + rr + frac) * cell;

            // Nonzero-winding scanline: gather signed crossings from every
            // sub-polygon together (each edge's sign follows its own
            // vertical direction, independent of which polygon it came
            // from), sort by x, then walk accumulating a running winding
            // number -- a span is inside wherever that running total is
            // nonzero. A hole sub-polygon's opposite winding direction is
            // what cancels the outer contour's winding back to zero over
            // its interior, exactly matching how Illustrator itself fills
            // a compound path.
            std::vector<std::pair<double, int>> crossings;
            for (const auto& poly : subpolygons) {
                size_t n = poly.size();
                if (n < 3) continue;
                for (size_t e = 0; e < n; ++e) {
                    const Vec2& A = poly[e];
                    const Vec2& B = poly[(e + 1) % n];
                    double ay = A.y, by = B.y;
                    if (ay == by) continue;
                    if ((yTest >= ay && yTest < by) || (yTest >= by && yTest < ay)) {
                        double t = (yTest - ay) / (by - ay);
                        double x = A.x + t * (B.x - A.x);
                        crossings.push_back({x, (by > ay) ? 1 : -1});
                    }
                }
            }
            if (crossings.size() < 2) continue;
            std::sort(crossings.begin(), crossings.end(),
                      [](const std::pair<double, int>& a, const std::pair<double, int>& b) { return a.first < b.first; });

            int winding = 0;
            for (size_t i = 0; i + 1 < crossings.size(); ++i) {
                winding += crossings[i].second;
                if (winding == 0) continue; // span to the right of this crossing is outside

                long cFrom = CellFloor(crossings[i].first - gx, cell) - colMin;
                long cTo = CellCeil(crossings[i + 1].first - gx, cell) - 1 - colMin;
                if (cFrom < 0) cFrom = 0;
                if (cTo > nCols - 1) cTo = nCols - 1;
                for (long cc = cFrom; cc <= cTo; ++cc) {
                    grid[(size_t)rr][(size_t)cc] = true;
                    anyFilled = true;
                }
            }
        }
    }

    if (!anyFilled) {
        if (nCols * nRows <= 4) {
            for (long rr = 0; rr < nRows; ++rr)
                for (long cc = 0; cc < nCols; ++cc) grid[(size_t)rr][(size_t)cc] = true;
            anyFilled = true;
        } else {
            return result; // degenerate: empty contours, not tooBig
        }
    }

    result.contours = TraceGridBoundary(grid, nCols, nRows, colMin, rowMin, cell, gx, gy, simplify);
    return result;
}

} // namespace kaku

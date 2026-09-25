// KakuMath.h
//
// SDK-independent geometry core for Kaku's two modes: 像素化 (rasterize a
// path to a grid, RasterizeToContours) and 多邊形 (replace each curved
// segment with straight chords, PolygonizeSegments). No Illustrator headers
// are used here on purpose -- this file/its .cpp can be compiled and
// sanity-tested standalone before being linked into the actual AILiveEffect
// plugin. The AIArt-level compound path construction RasterizeToContours'
// multi-contour results need lives in KakuPlugin.cpp, not here -- this file
// stays SDK-independent.

#pragma once

#include <vector>

namespace kaku {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
};

// One cubic Bezier segment of the path being processed: p0/p3 are the two
// anchors, p1/p2 are their ABSOLUTE (not relative) direction-handle points
// -- same convention as Illustrator's own AIPathSegment.in/out and the DOM's
// leftDirection/rightDirection, which is what the jsx's bezierAt() already
// assumed.
struct BezierSeg {
    Vec2 p0, p1, p2, p3;
};

// kBlock (像素化) is the default. kPolygon (多邊形) is a second, unrelated
// algorithm: not grid-based at all, see PolygonizeSegments below. (A third
// mode, 邊緣吸附/kOutline -- corner-snap to grid, preserving original
// angles -- existed earlier and was removed entirely at the user's
// request; SnapToGrid, which existed only to support it, was removed
// alongside it.)
enum class PixelateMode { kBlock, kPolygon };

struct GridParams {
    double density = 3.0;    // samples per cell along a segment; clamped [1,20] by the dialog.
                              // Unused by kPolygon.
    PixelateMode mode = PixelateMode::kBlock;
    double cellRatio = 8.0;  // cells across the selection's longer side, clamped [1,200] -- the
                              // only cell-sizing mechanism now (fixed-pt sizing was removed: grid
                              // origin is always the selection's own top-left, never document (0,0),
                              // and every path always simplifies collinear runs -- neither is a
                              // user-facing choice anymore). Unused by kPolygon.
    int facets = 2;          // kPolygon only: chords per curved segment, clamped [1,6] by the dialog.
};

// Cell size actually used for processing: the selection's bounding box's
// longer side (max of boundsW/boundsH) divided by cellRatio. Falls back to
// 1.0 if the result would be non-finite or <= 0.
double EffectiveCellFromRatio(double cellRatio, double boundsW, double boundsH);

// Evaluate a single cubic Bezier segment at parameter t in [0,1].
Vec2 BezierAt(const BezierSeg& seg, double t);

// Approximate arc length of one segment (12-sample polyline), same
// resolution the jsx's segLength() uses.
double SegLength(const BezierSeg& seg);

// Resample a path's segments into a fine polyline at ~`spacing` intervals.
// `closed` controls whether the last segment wraps from the final anchor
// back to the first. Mirrors resample() in the jsx, including its per-
// segment step cap (600) and its overall point cap (`maxPts`); `hitCap` is
// set true if either cap was hit (caller may want to report that, matching
// the jsx's `stats.capped`).
std::vector<Vec2> ResamplePath(const std::vector<BezierSeg>& segs, bool closed,
                                double spacing, size_t maxPts, bool* hitCap);

// Remove points that lie exactly on the line between their neighbors.
// Mirrors simplifyCollinear(); if the result would collapse below the
// minimum valid size (2 open / 3 closed) it returns the input unchanged,
// same fallback the jsx uses.
std::vector<Vec2> SimplifyCollinear(const std::vector<Vec2>& pts, bool closed);

// Degenerate-collapse fallback shared by both modes: when the grid is too
// coarse relative to the shape, returns a single grid-aligned rectangle
// (4 points, closed) covering `pts`'s bounding box, at least one cell in
// size. Mirrors the jsx's boundingBoxCells(). Returns an empty vector only
// if `pts` is empty.
std::vector<Vec2> BoundingBoxCells(const std::vector<Vec2>& pts, double cell, double gx, double gy);

// Result of rasterizing a closed polygon to a pixel grid: normally one
// contour, but a thin/concave shape can split into several disjoint
// islands, or gain a hole -- `contours` holds one closed rectilinear loop
// per piece (opposite winding direction for holes vs outer boundaries,
// which is what makes Illustrator's nonzero-winding fill render them as
// holes once each loop becomes a compound-path sub-path). `tooBig` is set
// instead of raising an exception when the grid would need more than 2M
// cells (caller should skip this path rather than hang); an empty,
// non-tooBig result means the shape is genuinely too small/thin for this
// cell size to catch anything (degenerate -- caller may want the
// BoundingBoxCells fallback instead).
struct RasterizeResult {
    std::vector<std::vector<Vec2>> contours;
    bool tooBig = false;
};

// Rasterize a fine, already-resampled closed polygon (e.g. ResamplePath's
// output on a closed path) into grid-aligned rectilinear contours. Uses a
// conservative "cell counts as filled if the shape touches it at all" rule
// with 3 sub-scanlines per row -- not simple center-point sampling -- so
// thin or curvy strokes don't vanish between sample points (this is the
// exact fix applied to the jsx after a real bug report: a cursive glyph's
// stroke going from "0 條路徑" at certain cell sizes to reliably filling).
// `simplify` controls whether collinear runs get collapsed in each
// resulting contour -- callers always pass true now (simplification is no
// longer a user-facing toggle), the parameter stays for standalone testing.
RasterizeResult RasterizeToContours(const std::vector<Vec2>& polygon, double cell,
                                     double gx, double gy, bool simplify);

// Same as RasterizeToContours, but for a *compound* path's sub-paths taken
// together, filled with the nonzero-winding rule instead of each polygon's
// own interior in isolation. This is what makes a genuine hole (a donut's
// inner circle, a letterform's counter like the bowl of "P") survive as a
// hole in the pixelated result.
//
// Why this exists as a separate function instead of just calling
// RasterizeToContours once per sub-path: verified empirically (not assumed)
// that RasterizeToContours's output orientation is *independent* of the
// input polygon's winding direction -- rasterizing the same circle from a
// clockwise vs counter-clockwise input produces the exact same output
// orientation both times. That's fine for a single simple shape, but it
// means processing a compound path's sub-paths independently always
// destroys the "this one is a hole" relationship between them: both the
// outer contour and the inner counter end up the same handedness after
// independent rasterization, so Illustrator's nonzero-winding fill sees
// them as additive, not subtractive -- the hole quietly disappears. Each
// sub-polygon's own direction has to stay meaningful all the way through
// the scanline fill test, which requires filling all of them in one pass.
RasterizeResult RasterizeCompoundToContours(const std::vector<std::vector<Vec2>>& subpolygons,
                                             double cell, double gx, double gy, bool simplify);

// 多邊形 mode: direct port of Polygonize.jsx's polygonize() -- turns each
// curved segment into straight chords, at "chamfer 0%" (no corner
// rounding/beveling, just the raw facet count). Unlike every other function
// in this file, this is NOT grid-based -- it works directly on the path's own
// existing segments, not a fine arc-length resample. Each segment that
// actually curves (its handles aren't coincident with its anchors) gets
// replaced by `facets` straight chords sampled at t = 1/facets ..
// (facets-1)/facets; a segment that's already a straight line (both handles
// sitting exactly on their anchors) is left as a single edge, untouched --
// this is what keeps existing straight edges crisp instead of needlessly
// subdividing them. Output points are always plain corners (no handles),
// same convention as every other mode's output.
std::vector<Vec2> PolygonizeSegments(const std::vector<BezierSeg>& segs, bool closed, int facets);

} // namespace kaku

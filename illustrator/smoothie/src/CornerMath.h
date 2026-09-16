// CornerMath.h
//
// SDK-independent corner-rounding geometry core for the Smoothie live effect.
// No Illustrator headers are used here on purpose: this file/its .cpp can be
// compiled and unit-tested standalone (see CornerMathTest.cpp), then linked
// into the actual AILiveEffect plugin once wired up in Xcode.
//
// 2026-09-15: replaced the earlier "vector interpolation" construction
// (handle length lerped between 0 and the circle-Bezier kappa constant)
// with a direct port of Glyphs' own Curvature tool logic, at the user's
// explicit request -- the interpolation version was judged too complex/
// not matching expected behavior. Glyphs' rule is much flatter: the
// handle length is simply a fixed proportion of the anchor distance, no
// reference to any circle-approximation constant at all.
//
// For vertex V with neighbors A (previous point) and C (next point):
//   u1 = normalize(A - V), u2 = normalize(C - V)      -- outward unit vectors
//   theta = angle(u1, u2)                              -- interior angle at V
//   D = R / tan(theta / 2)                              -- anchor distance
//   L = D * curvaturePercent                            -- handle length
//   P1 = V + u1*D,  H1 = P1 - u1*L                      -- P1/H1 collinear
//   P2 = V + u2*D,  H2 = P2 - u2*L                      -- P2/H2 collinear
//
// curvaturePercent is 0..1 (UI: 0%-100%, default 65%). Because L = D*C
// with C <= 1, the handle can never pass V (C=1 puts the handle exactly
// at V). There is no "100% = exact circle" property here -- C around
// 0.5523 (the circle-Bezier kappa constant) approximates a circle; higher
// C overshoots into a fuller, flatter-at-the-tip corner, lower C pulls
// toward a sharper chord. That is the real Glyphs Curvature slider's
// actual behavior, not a design choice made here.

#pragma once

#include <vector>

namespace smoothie {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    double dot(const Vec2& o) const { return x * o.x + y * o.y; }
    double cross(const Vec2& o) const { return x * o.y - y * o.x; }
    double length() const;
    Vec2 normalized() const;
    Vec2 perp() const { return {-y, x}; } // rotate +90 degrees (CCW)
};

struct CornerParams {
    double radius = 10.0;           // nominal corner radius, document units
    double curvaturePercent = 0.65; // 0..1 (Glyphs-style): handle length = anchor-distance D * this
};

struct BezierSeg {
    Vec2 p0, p1, p2, p3;
};

struct CornerResult {
    bool valid = false;     // false => corner left untouched (too sharp/shallow/degenerate)
    Vec2 trimStart;         // new endpoint of the incoming straight segment (on A-B)
    Vec2 trimEnd;           // new start point of the outgoing straight segment (on B-C)
    BezierSeg curve;        // the single Bezier segment replacing the corner
};

// Computes the corner replacement geometry for vertex B, given its two
// straight-line neighbors A (previous point) and C (next point).
// This models one polygon vertex; callers walk each path's vertices and
// splice the returned curve/trim points in place of the original corner.
CornerResult ComputeCorner(const Vec2& A, const Vec2& B, const Vec2& C, const CornerParams& params);

// Curvature of a cubic bezier segment at t=0, t=1, or an arbitrary t.
// Used by the test harness to verify the curvature profile numerically,
// and reusable by the plugin for diagnostics.
double CurvatureAtStart(const BezierSeg& seg);
double CurvatureAtEnd(const BezierSeg& seg);
double CurvatureAt(const BezierSeg& seg, double t);

} // namespace smoothie

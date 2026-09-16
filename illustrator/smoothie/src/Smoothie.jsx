// Smoothie.jsx
//
// Curvature Percentage (vector interpolation) corner rounding for Adobe
// Illustrator, as a standalone ExtendScript -- no plugin/compile needed,
// runs directly via File > Scripts > Smoothie.jsx (or double-click).
//
// Algorithm (as specified):
//   R = input radius
//   C = curvature percentage, 0.0..1.0, default 0.65
//
//   D = R + (R * 0.5) * C          // anchor extension distance
//       standard corner distance R at C=0, up to 1.5R at C=1
//   base_L = R * 0.55228           // circle-Bezier kappa handle length
//   max_L  = R * 0.825
//   L = base_L + (max_L - base_L) * C
//
//   For vertex V with unit edge directions v1 (toward the previous
//   anchor) and v2 (toward the next anchor):
//     P0 = V + v1*D,  C0 = P0 - v1*L      (anchor + handle on the v1 side)
//     P3 = V + v2*D,  C3 = P3 - v2*L      (anchor + handle on the v2 side)
//
// At C=0 this reduces to D=R, L=R*0.55228 -- the exact classic circular-
// arc corner (same formula Illustrator's own "Round Corners" effect and
// this project's native Smoothie plugin use for a pure circle). As C
// rises toward 1.0, both the anchor extension and the handle length grow
// beyond the circle values, producing a fuller, more "flowing" corner
// that reaches further along the edges -- not just a longer handle on a
// fixed-size arc.
//
// Since D can reach 1.5x the radius, a corner on a short edge can now
// extend further than a plain radius-R corner would: this script clamps
// D (and proportionally L) per-corner so it never eats more than half of
// either adjacent edge, exactly like the native plugin's safety clamp,
// to avoid corners overlapping or a fillet overshooting past its
// neighbor. That clamp is NOT part of the formula above -- it's a
// defensive addition, called out here explicitly.

#target illustrator

function computeCornerBezier(V, v1, v2, R, C) {
    // v1, v2 MUST be unit vectors (caller's responsibility, enforced by
    // normalize() below in ProcessPath).
    var D = R + (R * 0.5) * C;
    var baseL = R * 0.55228;
    var maxL = R * 0.825;
    var L = baseL + (maxL - baseL) * C;

    var P0 = [V[0] + v1[0] * D, V[1] + v1[1] * D];
    var C0 = [P0[0] - v1[0] * L, P0[1] - v1[1] * L];
    var P3 = [V[0] + v2[0] * D, V[1] + v2[1] * D];
    var C3 = [P3[0] - v2[0] * L, P3[1] - v2[1] * L];

    return { P0: P0, C0: C0, P3: P3, C3: C3, D: D, L: L };
}

function vSub(a, b) { return [a[0] - b[0], a[1] - b[1]]; }
function vLen(a) { return Math.sqrt(a[0] * a[0] + a[1] * a[1]); }
function vNorm(a) {
    var len = vLen(a);
    if (len < 1e-9) return [0, 0];
    return [a[0] / len, a[1] / len];
}

// Rounds every corner of one PathItem in place, given a radius (in the
// document's ruler/point units matching PathPoint.anchor's coordinate
// space) and a curvature percentage 0..1.
function roundPathCorners(pathItem, radius, curvaturePercent) {
    if (radius <= 0) return;
    var srcPoints = pathItem.pathPoints;
    var n = srcPoints.length;
    if (n < 3) return; // nothing to round

    var closed = pathItem.closed;
    var anchors = [];
    for (var i = 0; i < n; i++) anchors.push(srcPoints[i].anchor);

    // Compute, for every vertex, the corner replacement (or null if this
    // vertex is an open-path endpoint, or the corner is degenerate).
    var results = [];
    for (var i = 0; i < n; i++) {
        if (!closed && (i === 0 || i === n - 1)) { results.push(null); continue; }

        var A = anchors[(i - 1 + n) % n];
        var V = anchors[i];
        var Cn = anchors[(i + 1) % n];

        var rawV1 = vSub(A, V);
        var rawV2 = vSub(Cn, V);
        var lenAB = vLen(rawV1);
        var lenBC = vLen(rawV2);
        if (lenAB < 1e-6 || lenBC < 1e-6) { results.push(null); continue; }

        var v1 = vNorm(rawV1);
        var v2 = vNorm(rawV2);

        // Defensive clamp (not part of the given formula): D can reach
        // 1.5x radius, so cap it -- and scale L down to match -- so a
        // corner never eats more than half of either adjacent edge.
        var r = radius;
        var result = computeCornerBezier(V, v1, v2, r, curvaturePercent);
        var maxD = 0.5 * Math.min(lenAB, lenBC);
        if (result.D > maxD && result.D > 1e-9) {
            var scale = maxD / result.D;
            r = r * scale;
            result = computeCornerBezier(V, v1, v2, r, curvaturePercent);
        }
        if (result.D < 1e-6) { results.push(null); continue; }

        results.push(result);
    }

    // Rebuild the point list: each untouched vertex stays a single
    // corner point; each rounded vertex becomes two smooth points
    // (P0/C0 and P3/C3) with a straight run into/out of them.
    var newAnchors = [];
    var newLeft = [];   // leftDirection  (incoming handle)
    var newRight = [];  // rightDirection (outgoing handle)
    var newCorner = []; // PointType

    for (var i = 0; i < n; i++) {
        var r = results[i];
        if (!r) {
            var p = anchors[i];
            newAnchors.push(p); newLeft.push(p); newRight.push(p); newCorner.push(true);
            continue;
        }
        // P0: straight in from the previous point, curve out toward P3.
        newAnchors.push(r.P0); newLeft.push(r.P0); newRight.push(r.C0); newCorner.push(true);
        // P3: curve in from P0's handle, straight out toward the next point.
        newAnchors.push(r.P3); newLeft.push(r.C3); newRight.push(r.P3); newCorner.push(false);
    }

    // Apply: grow the path to the new point count, then set every point.
    // Illustrator's scripting DOM only lets you add points one at a time
    // (no bulk "SetPathSegments" like the native SDK), so remove all
    // existing points and re-add from scratch in order.
    while (pathItem.pathPoints.length > 0) {
        pathItem.pathPoints[pathItem.pathPoints.length - 1].remove();
    }
    for (var i = 0; i < newAnchors.length; i++) {
        var pt = pathItem.pathPoints.add();
        pt.anchor = newAnchors[i];
        pt.leftDirection = newLeft[i];
        pt.rightDirection = newRight[i];
        pt.pointType = newCorner[i] ? PointType.CORNER : PointType.SMOOTH;
    }
}

function main() {
    if (app.documents.length === 0) {
        alert("開啟一個文件並選取路徑後再執行。");
        return;
    }
    var sel = app.activeDocument.selection;
    if (!sel || sel.length === 0) {
        alert("請先選取至少一個路徑(PathItem)。");
        return;
    }

    // Minimal input: default curvature 0.65 as specified; radius has no
    // given default, so ask for it (in the document's current ruler unit
    // label just for the prompt text -- the value typed is interpreted
    // directly in points, matching pathPoints.anchor's coordinate space).
    var radiusStr = prompt("半徑(R,單位:pt):", "10");
    if (radiusStr === null) return;
    var radius = parseFloat(radiusStr);
    if (isNaN(radius) || radius <= 0) { alert("半徑必須是正數。"); return; }

    var curvatureStr = prompt("曲率百分比(C,0.0 ~ 1.0):", "0.65");
    if (curvatureStr === null) return;
    var curvature = parseFloat(curvatureStr);
    if (isNaN(curvature)) { alert("曲率百分比必須是數字。"); return; }
    if (curvature < 0) curvature = 0;
    if (curvature > 1) curvature = 1;

    var processed = 0;
    for (var i = 0; i < sel.length; i++) {
        var item = sel[i];
        if (item.typename === "PathItem") {
            roundPathCorners(item, radius, curvature);
            processed++;
        } else if (item.typename === "CompoundPathItem") {
            for (var j = 0; j < item.pathItems.length; j++) {
                roundPathCorners(item.pathItems[j], radius, curvature);
            }
            processed++;
        }
    }

    if (processed === 0) {
        alert("選取的項目裡沒有找到可以處理的路徑(PathItem / CompoundPathItem)。");
    } else {
        app.redraw();
    }
}

main();

// KakuPlugin.cpp
//
// Kaku -- an Illustrator Live Effect ("Effect > SFF Features >
// Kaku..." in the Appearance panel), built the same way as this
// developer's existing Smoothie plugin (samplecode/Smoothie). Two modes:
// 像素化 (rasterize a path to a grid, KakuMath.h's RasterizeToContours) and
// 多邊形 (replace each curved segment with straight chords,
// PolygonizeSegments). A third mode, 邊緣吸附 (corner-snap to grid,
// preserving original angles -- the original port of grid-quantize.jsx's
// processPathOutline), existed earlier and was removed entirely at the
// user's request; ProcessOnePathOutline and SnapToGrid, which existed only
// to support it, were deleted alongside it.
//
// Architecture verified against, and copied from, Smoothie's
// SmoothiePlugin.cpp (itself verified against the SDK's own TwirlFilter
// sample, the only sample that exercises AILiveEffect end to end):
//   - Plugins are a C++ class deriving from the SDK's `Plugin` base class
//     (samplecode/common/includes/Plugin.hpp); PluginMain lives in the
//     shared common source and dispatches into AllocatePlugin/FixupReload.
//   - Segment count changes go through sAIPath->SetPathSegmentCount()
//     followed by sAIPath->SetPathSegments() on the same path art.
//   - pb->art can be a group or compound path, so GoLiveEffect recurses
//     (kGroupArt / kCompoundPathArt) the same way Smoothie's
//     RoundArtRecursive does.
//   - Effect parameters live in an AILiveEffectParameters dictionary read
//     via KakuEffectParams.h's ReadParams/WriteParams.
//   - After changing parameters, call
//     sAILiveEffect->UpdateParameters(message->context) to trigger a re-run.
//
// Block mode's art-tree surgery (verified against AIArtSuite::NewArt's own
// header doc in AIArt.h, not guessed):
//   - AILiveEffectGoMessage::art is documented [in, out]: "You can set this
//     to NULL if the effect does not return any art; in this case,
//     Illustrator disposes of the input art" -- which establishes that
//     reassigning it to a *different* valid art handle (not just NULL) is
//     the sanctioned way to hand back replacement art, not merely mutating
//     the same handle's contents.
//   - The same doc says the plug-in "must not modify any ancestor objects,
//     nor modify the document artwork tree" -- read as "stay inside the
//     subtree GoLiveEffect was handed", not "never restructure it": NewArt's
//     own example code creates paths *inside* an existing group/compound
//     path via `kPlaceInsideOnTop`, which is exactly what turning one leaf
//     path into a multi-contour compound path needs.
//   - NewArt's doc also flags that art created while an effect is running
//     defaults to a plain black-fill/no-stroke style, so the original
//     path's style must be copied onto the new compound path explicitly
//     (AIPathStyleSuite::GetPathStyle/SetPathStyle) or the pixelated
//     result would render solid black regardless of the original color.
//
// KAKU_HAVE_AI_SDK gates all of this so the file keeps compiling
// (as a no-op stub) before the SDK headers are wired into an Xcode target.

#include "KakuMath.h"
#include "KakuEffectParams.h"

#include <cstring>
#include <vector>

#ifndef KAKU_HAVE_AI_SDK
#define KAKU_HAVE_AI_SDK 0
#endif

#if KAKU_HAVE_AI_SDK

#include "IllustratorSDK.h"
#include "Plugin.hpp"
#include "SDKDef.h"
#include "KakuSuites.h"
#include "KakuID.h"
#include "KakuOptionsPanel.h"

using namespace kaku;

// ---------------------------------------------------------------------
// Path <-> polyline bridge, same shape as Smoothie's RoundOnePath.
// ---------------------------------------------------------------------

static Vec2 ToVec2(const AIRealPoint& p) { return {p.h, p.v}; }
static AIRealPoint ToAIPoint(const Vec2& v) { return AIRealPoint{ (AIReal)v.x, (AIReal)v.y }; }

static void PointsToSegments(const std::vector<Vec2>& pts, std::vector<AIPathSegment>* out) {
    out->resize(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        AIRealPoint rp = ToAIPoint(pts[i]);
        (*out)[i].p = rp;
        (*out)[i].in = rp;
        (*out)[i].out = rp;
        (*out)[i].corner = true;
    }
}

static ASErr ReadPathAsBeziers(AIArtHandle path, bool closed, std::vector<BezierSeg>* outBeziers) {
    ai::int16 segCount = 0;
    ASErr error = sAIPath->GetPathSegmentCount(path, &segCount);
    if (error) return error;
    if (segCount < 2) return kNoErr;

    std::vector<AIPathSegment> segs(segCount);
    error = sAIPath->GetPathSegments(path, 0, segCount, segs.data());
    if (error) return error;

    size_t last = closed ? (size_t)segCount : (size_t)segCount - 1;
    outBeziers->reserve(last);
    for (size_t i = 0; i < last; ++i) {
        const AIPathSegment& a = segs[i];
        const AIPathSegment& b = segs[(i + 1) % (size_t)segCount];
        outBeziers->push_back({ ToVec2(a.p), ToVec2(a.out), ToVec2(b.in), ToVec2(b.p) });
    }
    return kNoErr;
}

// ---------------------------------------------------------------------
// Mode: 像素化 (block / rasterize). Only supports closed paths
// (matches the jsx). Single-contour results mutate `path` in place;
// multi-contour results (the shape split into islands, or gained a hole)
// build a brand-new compound path as `path`'s sibling and hand it back via
// `outReplacement` -- the caller disposes `path` and, if `outReplacement`
// is the top-level GoLiveEffect art itself, reassigns message->art to it.
// ---------------------------------------------------------------------
static ASErr ProcessOnePathBlock(AIArtHandle path, const GridParams& params, double cell, double gx, double gy,
                                  AIArtHandle* outReplacement) {
    *outReplacement = nullptr;

    AIBoolean closed = false;
    ASErr error = sAIPath->GetPathClosed(path, &closed);
    if (error) return error;
    if (!closed) return kNoErr; // open paths unsupported in block mode, same as the jsx

    std::vector<BezierSeg> beziers;
    error = ReadPathAsBeziers(path, true, &beziers);
    if (error) return error;
    if (beziers.size() < 3) return kNoErr;

    double spacing = cell / (params.density > 0 ? params.density : 1.0);
    if (spacing < 0.2) spacing = 0.2;

    bool hitCap = false;
    (void)hitCap;
    auto raw = ResamplePath(beziers, true, spacing, 15000, &hitCap);
    if (raw.size() < 3) return kNoErr;

    RasterizeResult rasterized = RasterizeToContours(raw, cell, gx, gy, /*simplify=*/true);
    if (rasterized.tooBig) return kNoErr; // grid would need too many cells -- skip, matches the jsx's stats.toobig

    std::vector<std::vector<Vec2>> kept = std::move(rasterized.contours);
    if (kept.empty()) {
        // Degenerate: collapse to a single grid-aligned block covering the
        // shape's bounding box rather than giving up (this is "push the
        // cell size to the extreme", not a bug -- see grid-quantize.jsx's
        // boundingBoxCells for the original report).
        auto rectPts = BoundingBoxCells(raw, cell, gx, gy);
        if (rectPts.size() < 4) return kNoErr;
        kept.push_back(std::move(rectPts));
    }

    if (kept.size() == 1) {
        std::vector<AIPathSegment> newSegs;
        PointsToSegments(kept[0], &newSegs);
        error = sAIPath->SetPathSegmentCount(path, (ai::int16)newSegs.size());
        if (error) return error;
        error = sAIPath->SetPathSegments(path, 0, (ai::int16)newSegs.size(), newSegs.data());
        if (error) return error;
        return sAIPath->SetPathClosed(path, true);
    }

    // Multiple contours: build a compound path sibling, one sub-path per
    // contour, then copy the original's appearance onto it (see this
    // file's header comment for why that copy is required).
    AIArtHandle compoundPath = nullptr;
    error = sAIArt->NewArt(kCompoundPathArt, kPlaceAbove, path, &compoundPath);
    if (error) return error;

    for (auto& contour : kept) {
        AIArtHandle subPath = nullptr;
        error = sAIArt->NewArt(kPathArt, kPlaceInsideOnTop, compoundPath, &subPath);
        if (error) return error;

        std::vector<AIPathSegment> subSegs;
        PointsToSegments(contour, &subSegs);
        error = sAIPath->SetPathSegmentCount(subPath, (ai::int16)subSegs.size());
        if (error) return error;
        error = sAIPath->SetPathSegments(subPath, 0, (ai::int16)subSegs.size(), subSegs.data());
        if (error) return error;
        error = sAIPath->SetPathClosed(subPath, true);
        if (error) return error;
    }

    AIPathStyle style;
    style.Init();
    AIBoolean hasAdvFill = false;
    error = sAIPathStyle->GetPathStyle(path, &style, &hasAdvFill);
    if (!error) sAIPathStyle->SetPathStyle(compoundPath, &style);

    *outReplacement = compoundPath;
    return kNoErr;
}

// ---------------------------------------------------------------------
// Compound-path-aware block mode: rasterizes ALL of `compoundPath`'s
// sub-paths together with nonzero-winding fill instead of one at a time,
// so a genuine hole (a donut's inner circle, a letterform's counter like
// the bowl of "P") survives as a hole -- see KakuMath.h's comment
// on RasterizeCompoundToContours for why per-subpath processing broke this
// (verified empirically: the plain rasterizer's output orientation doesn't
// depend on input winding at all, so two independently-processed
// sub-paths always end up the same handedness and Illustrator's
// nonzero-winding rule then fills both as solid instead of one being a
// hole in the other).
//
// Reuses the SAME compound-path handle -- disposes its old sub-paths and
// adds new ones inside it, rather than building a whole new compound path
// as a sibling -- which sidesteps the "new art defaults to black fill/no
// stroke" issue entirely, since a compound path's own style lives on the
// compound path itself, not on its individual sub-paths.
// ---------------------------------------------------------------------
static ASErr ProcessOneCompoundPathBlock(AIArtHandle compoundPath, const GridParams& params,
                                          double cell, double gx, double gy) {
    std::vector<std::vector<Vec2>> subpolygons;
    std::vector<AIArtHandle> oldChildren;

    AIArtHandle child = nullptr;
    ASErr error = sAIArt->GetArtFirstChild(compoundPath, &child);
    while (!error && child) {
        short artType = kUnknownArt;
        error = sAIArt->GetArtType(child, &artType);
        if (error) break;

        if (artType == kPathArt) {
            oldChildren.push_back(child);

            AIBoolean closed = false;
            ASErr subErr = sAIPath->GetPathClosed(child, &closed);
            if (!subErr && closed) {
                std::vector<BezierSeg> beziers;
                subErr = ReadPathAsBeziers(child, true, &beziers);
                if (!subErr && beziers.size() >= 3) {
                    double spacing = cell / (params.density > 0 ? params.density : 1.0);
                    if (spacing < 0.2) spacing = 0.2;
                    bool hitCap = false;
                    (void)hitCap;
                    auto raw = ResamplePath(beziers, true, spacing, 15000, &hitCap);
                    if (raw.size() >= 3) subpolygons.push_back(std::move(raw));
                }
            }
            // Open sub-paths (or unreadable ones) are silently skipped from
            // the fill test, same "unsupported" treatment as a bare open
            // path in block mode -- but still get disposed below along with
            // every other old sub-path, since the compound path as a whole
            // is being rebuilt from the rasterize result.
        }

        AIArtHandle next = nullptr;
        if (!error) error = sAIArt->GetArtSibling(child, &next);
        child = next;
    }
    if (error) return error;
    if (subpolygons.empty()) return kNoErr; // nothing usable -- leave the compound path untouched

    RasterizeResult rasterized = RasterizeCompoundToContours(subpolygons, cell, gx, gy, /*simplify=*/true);
    if (rasterized.tooBig) return kNoErr;

    std::vector<std::vector<Vec2>> kept = std::move(rasterized.contours);
    if (kept.empty()) return kNoErr; // degenerate -- leave the compound path untouched

    for (AIArtHandle old : oldChildren) {
        error = sAIArt->DisposeArt(old);
        if (error) return error;
    }

    for (auto& contour : kept) {
        AIArtHandle subPath = nullptr;
        error = sAIArt->NewArt(kPathArt, kPlaceInsideOnTop, compoundPath, &subPath);
        if (error) return error;

        std::vector<AIPathSegment> subSegs;
        PointsToSegments(contour, &subSegs);
        error = sAIPath->SetPathSegmentCount(subPath, (ai::int16)subSegs.size());
        if (error) return error;
        error = sAIPath->SetPathSegments(subPath, 0, (ai::int16)subSegs.size(), subSegs.data());
        if (error) return error;
        error = sAIPath->SetPathClosed(subPath, true);
        if (error) return error;
    }
    return kNoErr;
}

// ---------------------------------------------------------------------
// Mode: 多邊形 (polygonize). Not grid-based at all -- mutates `path` in
// place, never changes its identity. `cell`/`gx`/`gy` are irrelevant here
// (no grid), so this only needs `params` for `facets`.
// ---------------------------------------------------------------------
static ASErr ProcessOnePathPolygon(AIArtHandle path, const GridParams& params) {
    AIBoolean closed = false;
    ASErr error = sAIPath->GetPathClosed(path, &closed);
    if (error) return error;

    std::vector<BezierSeg> beziers;
    error = ReadPathAsBeziers(path, closed, &beziers);
    if (error) return error;
    if (beziers.empty()) return kNoErr;

    auto pts = PolygonizeSegments(beziers, closed, params.facets);
    size_t minLen = closed ? 3 : 2;
    if (pts.size() < minLen) return kNoErr;

    std::vector<AIPathSegment> newSegs;
    PointsToSegments(pts, &newSegs);
    error = sAIPath->SetPathSegmentCount(path, (ai::int16)newSegs.size());
    if (error) return error;
    error = sAIPath->SetPathSegments(path, 0, (ai::int16)newSegs.size(), newSegs.data());
    if (error) return error;
    return sAIPath->SetPathClosed(path, closed);
}

// Dispatches to the mode `params.mode` selects. `outReplacement` is set
// (non-null) only when block mode replaced `path` with a new compound
// path; polygon mode never changes identity.
static ASErr ProcessOnePath(AIArtHandle path, const GridParams& params, double cell, double gx, double gy,
                             AIArtHandle* outReplacement) {
    *outReplacement = nullptr;
    if (params.mode == PixelateMode::kPolygon) return ProcessOnePathPolygon(path, params);
    return ProcessOnePathBlock(path, params, cell, gx, gy, outReplacement);
}

static ASErr GoLiveEffectRecursive(AIArtHandle art, const GridParams& params, double cell, double gx, double gy);

// Handles one art node, whichever kind it turns out to be -- used both for
// GoLiveEffect's top-level art and for each child while recursing.
// `outReplacement` is set only when the node itself got replaced by a new
// handle (block mode's multi-contour result for a *bare leaf path*); a
// compound path's own handle never changes identity, its children get
// swapped in place instead (ProcessOneCompoundPathBlock).
static ASErr ProcessArtNode(AIArtHandle art, const GridParams& params, double cell, double gx, double gy,
                             AIArtHandle* outReplacement) {
    if (outReplacement) *outReplacement = nullptr;

    short artType = kUnknownArt;
    ASErr error = sAIArt->GetArtType(art, &artType);
    if (error) return error;

    if (artType == kPathArt) {
        return ProcessOnePath(art, params, cell, gx, gy, outReplacement);
    }
    if (artType == kCompoundPathArt && params.mode == PixelateMode::kBlock) {
        return ProcessOneCompoundPathBlock(art, params, cell, gx, gy);
    }
    if (artType == kGroupArt || artType == kCompoundPathArt) {
        // Either a plain group, or a compound path in polygon mode --
        // polygon mode never rebuilds topology (it only replaces each
        // sub-path's own segments in place, preserving that sub-path's
        // original winding), so per-child processing is safe there; only
        // block mode needs the combined nonzero-winding treatment above.
        return GoLiveEffectRecursive(art, params, cell, gx, gy);
    }
    return kNoErr;
}

// Recurse into groups/compound paths the same way Smoothie's
// RoundArtRecursive does -- art handed to GoLiveEffect is not guaranteed
// to be a single bare path. Unlike Smoothie, a child path here can be
// *replaced* (block mode's multi-contour case), so this fetches each
// child's next sibling before it's possibly disposed, rather than after.
static ASErr GoLiveEffectRecursive(AIArtHandle art, const GridParams& params, double cell, double gx, double gy) {
    ASErr error = kNoErr;
    AIArtHandle child = nullptr;
    error = sAIArt->GetArtFirstChild(art, &child);
    if (error) return kNoErr; // no children -- `art` itself is handled by the caller (GoLiveEffect)

    while (child && !error) {
        AIArtHandle nextChild = nullptr;
        AIArtHandle replacement = nullptr;
        error = ProcessArtNode(child, params, cell, gx, gy, &replacement);
        if (!error) error = sAIArt->GetArtSibling(child, &nextChild); // before any disposal
        if (!error && replacement) error = sAIArt->DisposeArt(child);
        child = nextChild;
    }
    return error;
}

class KakuPlugin : public Plugin {
public:
    KakuPlugin(SPPluginRef pluginRef) : Plugin(pluginRef) {}
    virtual ~KakuPlugin() {}
    FIXUP_VTABLE_EX(KakuPlugin, Plugin);

protected:
    virtual ASErr Message(char* caller, char* selector, void* message) override;
    virtual ASErr StartupPlugin(SPInterfaceMessage* message) override;
    virtual ASErr GoLiveEffect(AILiveEffectGoMessage* message) override;
    virtual ASErr EditLiveEffectParameters(AILiveEffectEditParamMessage* message) override;

private:
    ASErr AddLiveEffects(SPInterfaceMessage* message);

    AILiveEffectHandle fEffect = nullptr;
};

Plugin* AllocatePlugin(SPPluginRef pluginRef) { return new KakuPlugin(pluginRef); }
void FixupReload(Plugin* plugin) { KakuPlugin::FixupVTable((KakuPlugin*)plugin); }

ASErr KakuPlugin::Message(char* caller, char* selector, void* message) {
    return Plugin::Message(caller, selector, message);
}

ASErr KakuPlugin::StartupPlugin(SPInterfaceMessage* message) {
    ASErr error = Plugin::StartupPlugin(message);
    if (error) return error;
    return AddLiveEffects(message);
}

ASErr KakuPlugin::AddLiveEffects(SPInterfaceMessage* message) {
    AILiveEffectData effectData{};
    AddLiveEffectMenuData menuData{};
    AIMenuItemHandle menuHandle = nullptr;

    char categoryStr[] = " SFF Features"; // same category as this developer's Smoothie plugin
    char nameStr[] = " Kaku...";

    effectData.self = message->d.self;
    effectData.name = nameStr;
    effectData.title = nameStr;
    effectData.majorVersion = 1;
    effectData.minorVersion = 0;
    effectData.prefersAsInput = (kGroupInputArt | kPathInputArt | kCompoundPathInputArt);
    effectData.styleFilterFlags = kPostEffectFilter;

    ASErr error = sAILiveEffect->AddLiveEffect(&effectData, &fEffect);
    if (error) return error;

    menuData.category = categoryStr;
    menuData.title = nameStr;
    menuData.options = 0;
    error = sAILiveEffect->AddLiveEffectMenuItem(fEffect, nameStr, &menuData, &menuHandle, nullptr);
    if (error) return error;

    return sAIMenu->UpdateMenuItemAutomatically(menuHandle, kAutoEnableMenuItemAction, 0, 0, kIfPath, 0, 0, 0);
}

ASErr KakuPlugin::GoLiveEffect(AILiveEffectGoMessage* message) {
    if (message->effect != fEffect) return kNoErr;
    if (!message->art) return kNoErr;

    GridParams params = ReadParams(message->parameters);

    // Union bounds of the art being processed -- the grid origin is always
    // its own top-left (選取範圍左上角, never document (0,0) -- that choice
    // was removed), and its longer side turns "跨向格數" into an actual
    // cell size (EffectiveCellFromRatio mirrors the jsx's
    // deriveEffectiveCell, ratio-only now).
    AIRealRect bounds{};
    ASErr boundsErr = sAIArt->GetArtBounds(message->art, &bounds);
    double gx = 0.0, gy = 0.0, cell = 1.0;
    if (!boundsErr) {
        double boundsW = bounds.right - bounds.left;
        double boundsH = bounds.top - bounds.bottom;
        gx = bounds.left;
        gy = bounds.top;
        cell = EffectiveCellFromRatio(params.cellRatio, boundsW, boundsH);
    }

    // ProcessArtNode handles every case uniformly: a bare leaf path (which
    // may need outright *replacement* -- block mode's multi-contour case,
    // the only situation that needs message->art's own [in, out] handle
    // reassigned), a compound path (rasterized as one unit in block mode so
    // holes survive, or recursed into per sub-path in polygon mode), or a
    // group (recursed into).
    AIArtHandle replacement = nullptr;
    ASErr error = ProcessArtNode(message->art, params, cell, gx, gy, &replacement);
    if (!error && replacement) {
        AIArtHandle old = message->art;
        message->art = replacement;
        error = sAIArt->DisposeArt(old);
    }
    return error;
}

// Native AppKit modal dialog (KakuOptionsPanel.mm), synchronous --
// same call-frame-stays-on-the-stack shape as Smoothie's
// EditLiveEffectParameters.
ASErr KakuPlugin::EditLiveEffectParameters(AILiveEffectEditParamMessage* message) {
    if (message->effect != fEffect) return kNoErr;

    GridParams original = ReadParams(message->parameters);
    GridParams result = original;
    bool okClicked = ShowOptionsDialog(message->parameters, message->context,
                                        original, message->allowPreview, &result);

    // Whether OK or Cancel, write the final state back explicitly -- Cancel
    // must restore `original` even if preview pushed intermediate values
    // into the dictionary while the dialog was open.
    WriteParams(message->parameters, okClicked ? result : original);
    sAILiveEffect->UpdateParameters(message->context);
    return kNoErr;
}

// NOTE: PluginMain itself is NOT defined here -- samplecode/common/source/Main.cpp
// already provides it (it dispatches to AllocatePlugin/FixupReload above).
// Defining it again here would be a duplicate-symbol link error.

#else // !KAKU_HAVE_AI_SDK

// Without the SDK present, this file still needs to compile (e.g. for
// VS Code indexing / IntelliSense). This stub makes that possible; it does
// nothing at runtime.
extern "C" int Kaku_PluginMainStub() { return 0; }

#endif // KAKU_HAVE_AI_SDK

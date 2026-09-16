// PluginMain.cpp
//
// Smoothie -- an Illustrator Live Effect ("Effect > SDK > Smoothie..."
// in the Appearance panel) that wraps the verified geometry in
// CornerMath.cpp/.h.
//
// 2026-09-15 reset: the CSXS/Spectrum-Web-Components panel + its native
// bridge (GeoCornersFlashController, an async OpenPanel() that stashed
// AILiveEffectParameters/AILiveEffectParamContext across a PlugPlug IPC
// round trip) is gone -- that whole design is what crashed Illustrator on
// clicking OK, and added a class of context-lifetime bugs that a plain
// native dialog doesn't have. EditLiveEffectParameters below now opens a
// synchronous AppKit dialog (SmoothieOptionsPanel.mm) instead: same
// call-frame-stays-on-the-stack shape AILiveEffect.h's own docs describe
// ("the plug-in receives a Go message after updating parameters").
//
// Everything else here is unchanged from before and still verified
// against the SDK's own `TwirlFilter` sample (the only sample that
// actually exercises AILiveEffect end to end):
//   - Plugins are a C++ class deriving from the SDK's `Plugin` base class
//     (samplecode/common/includes/Plugin.hpp), not a bare C PluginMain
//     switch statement. `PluginMain` itself lives in the SDK's common
//     source and dispatches into your subclass's virtual methods.
//   - AILiveEffectGoMessage::art is [in, out] -- mutate the SAME art
//     handle in place.
//   - Segment count changes go through sAIPath->SetPathSegmentCount()
//     followed by sAIPath->SetPathSegments() on the same path art.
//   - pb->art can be a group or compound path, so GoLiveEffect recurses
//     (kGroupArt / kCompoundPathArt) the same way TwirlFilter's DoTwirl()
//     does.
//   - Effect parameters live in an AILiveEffectParameters dictionary read
//     via sAIDictionary->Key()/IsKnown()/GetRealEntry()/SetRealEntry().
//   - After changing parameters, call
//     sAILiveEffect->UpdateParameters(message->context) to trigger a re-run.
//
// SMOOTHIE_HAVE_AI_SDK still gates all of this so the file keeps
// compiling (as a no-op stub) for editing before the SDK headers are
// wired into an actual Xcode target.

#include "CornerMath.h"
#include "CornerEffectParams.h"

#include <cstring>
#include <vector>

#ifndef SMOOTHIE_HAVE_AI_SDK
#define SMOOTHIE_HAVE_AI_SDK 0
#endif

#if SMOOTHIE_HAVE_AI_SDK

#include "IllustratorSDK.h"
#include "Plugin.hpp"
#include "SDKDef.h"
#include "SmoothieSuites.h"
#include "SmoothieID.h"
#include "SmoothieOptionsPanel.h"

using namespace smoothie;

// ---------------------------------------------------------------------
// Path <-> polyline bridge. Illustrator paths are already Bezier; we treat
// each anchor as a rounding candidate using the straight-line direction to
// its neighbors (same simplification the built-in Round Corners effect
// uses).
// ---------------------------------------------------------------------

static Vec2 ToVec2(const AIRealPoint& p) { return {p.h, p.v}; }
static AIRealPoint ToAIPoint(const Vec2& v) { return AIRealPoint{ (AIReal)v.x, (AIReal)v.y }; }

static ASErr RoundOnePath(AIArtHandle path, const CornerParams& params) {
    ai::int16 segCount = 0;
    ASErr error = sAIPath->GetPathSegmentCount(path, &segCount);
    if (error) return error;
    if (segCount < 3) return kNoErr; // nothing to round

    std::vector<AIPathSegment> segs(segCount);
    error = sAIPath->GetPathSegments(path, 0, segCount, segs.data());
    if (error) return error;

    AIBoolean closed = false;
    error = sAIPath->GetPathClosed(path, &closed);
    if (error) return error;

    std::vector<Vec2> verts(segCount);
    for (ai::int16 i = 0; i < segCount; ++i) verts[i] = ToVec2(segs[i].p);

    std::vector<AIPathSegment> newSegs;
    newSegs.reserve(segCount * 2);
    size_t n = verts.size();

    for (size_t i = 0; i < n; ++i) {
        if (!closed && (i == 0 || i == n - 1)) {
            newSegs.push_back(segs[i]); // endpoints of an open path: nothing to round
            continue;
        }

        const Vec2& A = verts[(i + n - 1) % n];
        const Vec2& B = verts[i];
        const Vec2& C = verts[(i + 1) % n];
        CornerResult r = ComputeCorner(A, B, C, params);

        if (!r.valid) {
            newSegs.push_back(segs[i]);
            continue;
        }

        AIPathSegment trimStart{};
        trimStart.p = trimStart.in = ToAIPoint(r.trimStart);
        trimStart.out = ToAIPoint(r.curve.p1);
        trimStart.corner = (params.curvaturePercent <= 0.0); // 0% is a beveled corner point
        newSegs.push_back(trimStart);

        AIPathSegment trimEnd{};
        trimEnd.p = trimEnd.out = ToAIPoint(r.trimEnd);
        trimEnd.in = ToAIPoint(r.curve.p2);
        trimEnd.corner = false;
        newSegs.push_back(trimEnd);
    }

    error = sAIPath->SetPathSegmentCount(path, (ai::int16)newSegs.size());
    if (error) return error;
    return sAIPath->SetPathSegments(path, 0, (ai::int16)newSegs.size(), newSegs.data());
}

// Recurse into groups/compound paths the same way TwirlFilter's DoTwirl()
// does -- pb->art handed to GoLiveEffect is not guaranteed to be a single
// bare path.
static ASErr RoundArtRecursive(AIArtHandle art, const CornerParams& params) {
    ASErr error = kNoErr;
    AIArtHandle child = nullptr;
    error = sAIArt->GetArtFirstChild(art, &child);
    if (error) {
        // No children -- if `art` itself is a path, round it directly.
        short artType = kUnknownArt;
        error = sAIArt->GetArtType(art, &artType);
        if (error) return error;
        if (artType == kPathArt) return RoundOnePath(art, params);
        return kNoErr;
    }
    while (child && !error) {
        short artType = kUnknownArt;
        error = sAIArt->GetArtType(child, &artType);
        if (error) break;
        if (artType == kPathArt) {
            error = RoundOnePath(child, params);
        } else if (artType == kGroupArt || artType == kCompoundPathArt) {
            error = RoundArtRecursive(child, params);
        }
        if (!error) error = sAIArt->GetArtSibling(child, &child);
    }
    return error;
}

class SmoothiePlugin : public Plugin {
public:
    SmoothiePlugin(SPPluginRef pluginRef) : Plugin(pluginRef) {}
    virtual ~SmoothiePlugin() {}
    FIXUP_VTABLE_EX(SmoothiePlugin, Plugin);

protected:
    virtual ASErr Message(char* caller, char* selector, void* message) override;
    virtual ASErr StartupPlugin(SPInterfaceMessage* message) override;
    virtual ASErr GoLiveEffect(AILiveEffectGoMessage* message) override;
    virtual ASErr EditLiveEffectParameters(AILiveEffectEditParamMessage* message) override;

private:
    ASErr AddLiveEffects(SPInterfaceMessage* message);

    AILiveEffectHandle fEffect = nullptr;
};

Plugin* AllocatePlugin(SPPluginRef pluginRef) { return new SmoothiePlugin(pluginRef); }
void FixupReload(Plugin* plugin) { SmoothiePlugin::FixupVTable((SmoothiePlugin*)plugin); }

ASErr SmoothiePlugin::Message(char* caller, char* selector, void* message) {
    return Plugin::Message(caller, selector, message);
}

ASErr SmoothiePlugin::StartupPlugin(SPInterfaceMessage* message) {
    ASErr error = Plugin::StartupPlugin(message);
    if (error) return error;
    return AddLiveEffects(message);
}

ASErr SmoothiePlugin::AddLiveEffects(SPInterfaceMessage* message) {
    AILiveEffectData effectData{};
    AddLiveEffectMenuData menuData{};
    AIMenuItemHandle menuHandle = nullptr;

    char categoryStr[] = " SFF Features"; // SFF = staff-from-foundry (yonghanciou), this plugin's developer
    char nameStr[] = " Smoothie...";

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

ASErr SmoothiePlugin::GoLiveEffect(AILiveEffectGoMessage* message) {
    if (message->effect != fEffect) return kNoErr;

    CornerParams params = ReadParams(message->parameters);
    if (!message->art) return kNoErr;
    return RoundArtRecursive(message->art, params);
}

// Native AppKit modal dialog (SmoothieOptionsPanel.mm), synchronous:
// this call does not return until the user clicks OK or Cancel, so
// message->parameters/message->context are only ever touched while this
// function is on the stack -- see PluginMain.cpp's header comment and
// SmoothieOptionsPanel.h for why that matters.
ASErr SmoothiePlugin::EditLiveEffectParameters(AILiveEffectEditParamMessage* message) {
    if (message->effect != fEffect) return kNoErr;

    CornerParams original = ReadParams(message->parameters);
    CornerParams result = original;
    bool okClicked = ShowOptionsDialog(message->parameters, message->context,
                                        original, message->allowPreview, &result);

    // Whether OK or Cancel, write the final state back explicitly rather
    // than relying on whatever the last live-preview write left behind:
    // Cancel must restore `original` even if preview pushed intermediate
    // values into the dictionary while the dialog was open.
    WriteParams(message->parameters, okClicked ? result : original);
    sAILiveEffect->UpdateParameters(message->context);
    return kNoErr;
}

// NOTE: PluginMain itself is NOT defined here -- samplecode/common/source/Main.cpp
// already provides it (it dispatches to AllocatePlugin/FixupReload above).
// Defining it again here would be a duplicate-symbol link error.

#else // !SMOOTHIE_HAVE_AI_SDK

// Without the SDK present, this file still needs to compile as part of the
// repo (e.g. for VS Code indexing / IntelliSense). This stub makes that
// possible; it does nothing at runtime.
extern "C" int Smoothie_PluginMainStub() { return 0; }

#endif // SMOOTHIE_HAVE_AI_SDK

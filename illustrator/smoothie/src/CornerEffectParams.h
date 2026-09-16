// CornerEffectParams.h
//
// Bridges the SDK-independent smoothie::CornerParams to the way an
// Illustrator Live Effect actually persists its parameters: a small
// AIDictionaryRef/AIEntry set attached to the art's Live Effect instance,
// read back every time Illustrator needs to re-run the effect (undo, zoom,
// "expand appearance", scripted batch, etc.) — this is what makes the
// effect show up in the Appearance panel and stay editable forever instead
// of being a one-shot destructive action.
//
// NOTE: This header intentionally avoids depending on the real Illustrator
// SDK headers (AIDictionary.h, AITypes.h, ...) so it stays readable and
// buildable before the SDK is wired in. The ReadParams/WriteParams bodies
// below are gated behind SMOOTHIE_HAVE_AI_SDK and match the *verified*
// real API (checked against the actual Illustrator 2026 SDK's
// illustratorapi/illustrator/AIDictionary.h and the TwirlFilter sample's
// use of it in TwirlFilter.cpp) -- not guessed.

#pragma once

#include "CornerMath.h"

#ifndef SMOOTHIE_HAVE_AI_SDK
#define SMOOTHIE_HAVE_AI_SDK 0
#endif

#if SMOOTHIE_HAVE_AI_SDK
#include "IllustratorSDK.h"
#include "SmoothieSuites.h" // sAIDictionary
#endif

namespace smoothie {

// Keys used in the Live Effect's parameter dictionary. Keep these stable
// once shipped -- they're what makes old documents keep re-editable
// appearance entries after an update.
//
// kMode/kOpticalStrength/kG2Flow from the Round/Optical/G2 era are
// deliberately NOT reused for curvaturePercent: an old document with those
// keys still parses fine (ReadParams below just won't find
// "smoothie.curvaturePercent" and falls back to the default), rather
// than risking a stale g2Flow value silently being reinterpreted as a
// curvature percentage.
namespace ParamKeys {
    constexpr const char* kRadius = "smoothie.radius";                     // real
    constexpr const char* kCurvaturePercent = "smoothie.curvaturePercent"; // real, 0..1
}

inline CornerParams DefaultParams() {
    CornerParams p;
    p.radius = 10.0;
    p.curvaturePercent = 0.65; // Glyphs' own default curvature
    return p;
}

// --- SDK-dependent glue ---------------------------------------------------
//
// `AILiveEffectParameters` (the real handle type for a live effect's
// parameter dictionary) is itself just an AIDictionaryRef under the hood.
// The read/write pattern below is copied from how the SDK's own TwirlFilter
// sample handles its "angle" parameter in TwirlFilter.cpp -- same
// Key()/IsKnown()/Get*Entry()/Set*Entry() calls.

#if SMOOTHIE_HAVE_AI_SDK

inline CornerParams ReadParams(AILiveEffectParameters dict) {
    CornerParams p = DefaultParams();
    AIDictKey key;

    key = sAIDictionary->Key(ParamKeys::kRadius);
    if (sAIDictionary->IsKnown(dict, key)) sAIDictionary->GetRealEntry(dict, key, &p.radius);

    key = sAIDictionary->Key(ParamKeys::kCurvaturePercent);
    if (sAIDictionary->IsKnown(dict, key)) sAIDictionary->GetRealEntry(dict, key, &p.curvaturePercent);

    return p;
}

inline void WriteParams(AILiveEffectParameters dict, const CornerParams& p) {
    sAIDictionary->SetRealEntry(dict, sAIDictionary->Key(ParamKeys::kRadius), (AIReal)p.radius);
    sAIDictionary->SetRealEntry(dict, sAIDictionary->Key(ParamKeys::kCurvaturePercent), (AIReal)p.curvaturePercent);
}

#endif // SMOOTHIE_HAVE_AI_SDK

} // namespace smoothie

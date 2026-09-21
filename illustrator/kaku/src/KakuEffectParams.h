// KakuEffectParams.h
//
// Bridges the SDK-independent kaku::GridParams to an
// AILiveEffectParameters dictionary, same pattern as Smoothie's
// CornerEffectParams.h (verified against the real Illustrator 2026 SDK's
// illustratorapi/illustrator/AIDictionary.h -- GetRealEntry/SetRealEntry/
// GetIntegerEntry/SetIntegerEntry, same shape Smoothie already uses for
// its own real-valued params).
//
// Deliberately small parameter set (fixed-pt cell sizing, document-origin
// choice, and the simplify-collinear toggle were all removed at the user's
// request -- grid sizing is ratio-only now, origin is always the
// selection's own top-left, and simplification always runs).

#pragma once

#include "KakuMath.h"

#ifndef KAKU_HAVE_AI_SDK
#define KAKU_HAVE_AI_SDK 0
#endif

#if KAKU_HAVE_AI_SDK
#include "IllustratorSDK.h"
#include "KakuSuites.h" // sAIDictionary
#endif

namespace kaku {

// Keys used in the Live Effect's parameter dictionary. Keep stable once
// shipped, same reasoning as Smoothie's ParamKeys.
namespace ParamKeys {
    constexpr const char* kDensity = "kaku.density";     // real, 1..20
    constexpr const char* kMode = "kaku.mode";           // int: 0 = block (完全像素化), 1 = outline (格點吸附)
    constexpr const char* kCellRatio = "kaku.cellRatio"; // real, 1..200
}

inline GridParams DefaultParams() {
    GridParams p;
    p.density = 3.0;
    p.mode = PixelateMode::kBlock;
    p.cellRatio = 8.0;
    return p;
}

#if KAKU_HAVE_AI_SDK

inline GridParams ReadParams(AILiveEffectParameters dict) {
    GridParams p = DefaultParams();
    AIDictKey key;

    key = sAIDictionary->Key(ParamKeys::kDensity);
    if (sAIDictionary->IsKnown(dict, key)) sAIDictionary->GetRealEntry(dict, key, &p.density);

    key = sAIDictionary->Key(ParamKeys::kMode);
    if (sAIDictionary->IsKnown(dict, key)) {
        ai::int32 v = (p.mode == PixelateMode::kOutline) ? 1 : 0;
        sAIDictionary->GetIntegerEntry(dict, key, &v);
        p.mode = (v == 1) ? PixelateMode::kOutline : PixelateMode::kBlock;
    }

    key = sAIDictionary->Key(ParamKeys::kCellRatio);
    if (sAIDictionary->IsKnown(dict, key)) sAIDictionary->GetRealEntry(dict, key, &p.cellRatio);

    return p;
}

inline void WriteParams(AILiveEffectParameters dict, const GridParams& p) {
    sAIDictionary->SetRealEntry(dict, sAIDictionary->Key(ParamKeys::kDensity), (AIReal)p.density);
    sAIDictionary->SetIntegerEntry(dict, sAIDictionary->Key(ParamKeys::kMode), (p.mode == PixelateMode::kOutline) ? 1 : 0);
    sAIDictionary->SetRealEntry(dict, sAIDictionary->Key(ParamKeys::kCellRatio), (AIReal)p.cellRatio);
}

#endif // KAKU_HAVE_AI_SDK

} // namespace kaku

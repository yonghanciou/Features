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
    constexpr const char* kMode = "kaku.mode";           // int: 0 = block (像素化), 2 = polygon (多邊形).
                                                          // 1 (邊緣吸附/kOutline) was removed and is intentionally
                                                          // left unassigned rather than reused, in case an old
                                                          // saved document still has mode=1 in its Live Effect
                                                          // params -- DecodeMode below falls back to kBlock for it.
    constexpr const char* kCellRatio = "kaku.cellRatio"; // real, 1..200
    constexpr const char* kFacets = "kaku.facets";       // int, 1..6 -- polygon mode only
}

inline GridParams DefaultParams() {
    GridParams p;
    p.density = 3.0;
    p.mode = PixelateMode::kBlock;
    p.cellRatio = 8.0;
    p.facets = 2;
    return p;
}

#if KAKU_HAVE_AI_SDK

namespace detail {
inline ai::int32 EncodeMode(PixelateMode m) {
    switch (m) {
        case PixelateMode::kPolygon: return 2;
        case PixelateMode::kBlock: default: return 0;
    }
}
inline PixelateMode DecodeMode(ai::int32 v) {
    if (v == 2) return PixelateMode::kPolygon;
    return PixelateMode::kBlock; // also the fallback for the old, removed mode=1 (邊緣吸附)
}
} // namespace detail

inline GridParams ReadParams(AILiveEffectParameters dict) {
    GridParams p = DefaultParams();
    AIDictKey key;

    key = sAIDictionary->Key(ParamKeys::kDensity);
    if (sAIDictionary->IsKnown(dict, key)) sAIDictionary->GetRealEntry(dict, key, &p.density);

    key = sAIDictionary->Key(ParamKeys::kMode);
    if (sAIDictionary->IsKnown(dict, key)) {
        ai::int32 v = detail::EncodeMode(p.mode);
        sAIDictionary->GetIntegerEntry(dict, key, &v);
        p.mode = detail::DecodeMode(v);
    }

    key = sAIDictionary->Key(ParamKeys::kCellRatio);
    if (sAIDictionary->IsKnown(dict, key)) sAIDictionary->GetRealEntry(dict, key, &p.cellRatio);

    key = sAIDictionary->Key(ParamKeys::kFacets);
    if (sAIDictionary->IsKnown(dict, key)) {
        ai::int32 v = p.facets;
        sAIDictionary->GetIntegerEntry(dict, key, &v);
        p.facets = (int)v;
    }

    return p;
}

inline void WriteParams(AILiveEffectParameters dict, const GridParams& p) {
    sAIDictionary->SetRealEntry(dict, sAIDictionary->Key(ParamKeys::kDensity), (AIReal)p.density);
    sAIDictionary->SetIntegerEntry(dict, sAIDictionary->Key(ParamKeys::kMode), detail::EncodeMode(p.mode));
    sAIDictionary->SetRealEntry(dict, sAIDictionary->Key(ParamKeys::kCellRatio), (AIReal)p.cellRatio);
    sAIDictionary->SetIntegerEntry(dict, sAIDictionary->Key(ParamKeys::kFacets), (ai::int32)p.facets);
}

#endif // KAKU_HAVE_AI_SDK

} // namespace kaku

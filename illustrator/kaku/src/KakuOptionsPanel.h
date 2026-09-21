// KakuOptionsPanel.h
//
// Native AppKit dialog, same shape as Smoothie's SmoothieOptionsPanel.h:
// plain C++ here (no Objective-C types) so KakuPlugin.cpp (compiled
// as plain C++, not Objective-C++) can call this without caring how the
// dialog is implemented. All the AppKit code lives in the .mm.

#pragma once

#include "KakuMath.h"

#if KAKU_HAVE_AI_SDK
#include "IllustratorSDK.h"

namespace kaku {

// Shows a modal options dialog seeded with `initial`. Blocks (pumping its
// own modal event loop the normal Cocoa way) until the user clicks OK or
// Cancel.
//
// While the "預覽" checkbox is on, every control change immediately writes
// the in-progress values into `parameters` and calls
// sAILiveEffect->UpdateParameters(context) so the canvas updates live --
// both `parameters` and `context` must stay valid for the whole call,
// which they do here because this never returns early.
//
// Returns true and fills *outParams if the user clicked OK. Returns false
// on Cancel; the caller (KakuPlugin.cpp's EditLiveEffectParameters)
// is responsible for restoring `parameters` to whatever was in effect
// before the dialog opened.
bool ShowOptionsDialog(AILiveEffectParameters parameters,
                        AILiveEffectParamContext context,
                        const GridParams& initial,
                        bool allowPreview,
                        GridParams* outParams);

} // namespace kaku

#endif // KAKU_HAVE_AI_SDK

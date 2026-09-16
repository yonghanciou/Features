// SmoothieOptionsPanel.h
//
// Reset (see README.md "2026-09-15 重新開始"): the CSXS/Spectrum-Web-
// Components panel + GeoCornersFlashController bridge is gone. It crashed
// Illustrator on OK and added a whole class of context-lifetime bugs for
// no benefit CornerMath.cpp's parameters actually needed. This replaces it
// with a plain native AppKit dialog, modal via runModalForWindow: -- the
// same call-stays-on-the-stack shape EditLiveEffectParameters already
// expects (see AILiveEffect.h: "the plug-in receives a Go message after
// updating parameters", i.e. synchronous).
//
// Deliberately plain C++ here (no Objective-C types) so PluginMain.cpp
// (compiled as plain C++, not Objective-C++) can call this without caring
// how the dialog is implemented. All the AppKit code lives in the .mm.

#pragma once

#include "CornerMath.h"

#if SMOOTHIE_HAVE_AI_SDK
#include "IllustratorSDK.h"

namespace smoothie {

// Shows a modal options dialog seeded with `initial`. Blocks (pumping its
// own modal event loop the normal Cocoa way) until the user clicks OK or
// Cancel.
//
// While the "Preview" checkbox is on, every control change immediately
// writes the in-progress values into `parameters` and calls
// sAILiveEffect->UpdateParameters(context) so the canvas updates live --
// both `parameters` and `context` must stay valid for the whole call,
// which they do here because we never return early the way the old
// fire-and-forget CSXS OpenPanel() did.
//
// Returns true and fills *outParams if the user clicked OK. Returns false
// on Cancel; the caller is responsible for restoring `parameters` to
// whatever was in effect before the dialog opened (this function does not
// track that -- see PluginMain.cpp's EditLiveEffectParameters, which reads
// the original params before calling this and re-applies them on a false
// return).
bool ShowOptionsDialog(AILiveEffectParameters parameters,
                        AILiveEffectParamContext context,
                        const CornerParams& initial,
                        bool allowPreview,
                        CornerParams* outParams);

} // namespace smoothie

#endif // SMOOTHIE_HAVE_AI_SDK

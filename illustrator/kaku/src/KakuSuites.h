// KakuSuites.h
// Same suite set as Smoothie's SmoothieSuites.h (this plugin is built the
// same way: AILiveEffect + AIDictionary for params, AIPath/AIArt for
// reading and rewriting path geometry, AIUITheme for the native dialog's
// dark/light colors). sAIUser, sSPAccess, sSPPlugins, sAINotifier,
// sAIAppContext, sAIFilePath, sAIFolders come from the shared
// common/includes/Suites.hpp already pulled in by IllustratorSDK.h.
//
// Phase 2 adds sAIPathStyle: 完全像素化 mode can replace a path with a new
// compound path (see KakuPlugin.cpp's ProcessOnePathBlock); per
// AIArtSuite::NewArt's own docs, art created while an effect is running
// gets a default black-fill/no-stroke style, so the original path's style
// has to be copied onto the new compound path explicitly via
// AIPathStyleSuite::GetPathStyle/SetPathStyle.

#ifndef __KAKUSUITES_H__
#define __KAKUSUITES_H__

#include "IllustratorSDK.h"
#include "Suites.hpp"
#include "AIStringFormatUtils.h"
#include "AIUITheme.h"
#include "AIPathStyle.h"

extern	"C"	AILiveEffectSuite*		sAILiveEffect;
extern	"C"	AIDictionarySuite*		sAIDictionary;
extern	"C"	AIMenuSuite*			sAIMenu;
extern	"C"	AIPathSuite*			sAIPath;
extern	"C"	AIRealMathSuite*		sAIRealMath;
extern	"C"	AIArtSuite*				sAIArt;
extern	"C"	AIUnicodeStringSuite*	sAIUnicodeString;
extern	"C"	AIStringFormatUtilsSuite*	sAIStringFormatUtils;
extern	"C"	SPBlocksSuite*			sSPBlocks;
// Native dialog theme colors (KakuOptionsPanel.mm). sAIUser is
// already declared by the shared common/includes/Suites.hpp.
extern	"C"	AIUIThemeSuite*			sAIUITheme;
extern	"C"	AIPathStyleSuite*		sAIPathStyle;

#endif // End KakuSuites.h

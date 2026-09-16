// SmoothieSuites.h
// Adapted from the SDK's TwirlFilterSuites.h (samplecode/TwirlFilter/Source/TwirlFilterSuites.h).
// sAIUser, sSPAccess, sSPPlugins, sAINotifier, sAIAppContext, sAIFilePath,
// sAIFolders are already provided by the shared common/includes/Suites.hpp
// -- only list suites here that TwirlFilterSuites.h doesn't already cover
// via that shared base set.
//
// 2026-09-15 reset: dropped sAIMdMemory -- grep confirms zero call sites
// anywhere in Source/ after removing the CSXS bridge that used to need it.
// sAIUnicodeString/sAIStringFormatUtils/sSPBlocks LOOK unused by our own
// Source/*.cpp the same way, but keep them: the "Shared" group's
// IAIUnicodeString.cpp/IAIStringFormatUtils.cpp (ai::UnicodeString /
// ai::NumberFormat's implementation, compiled into every SDK plugin
// including this one) reference these three as extern globals internally
// -- removing them is a **link** error (undefined symbol), not a dead
// code path, found the hard way by actually rebuilding after trimming
// them out. sAIUITheme stays for a real reason: the new native options
// dialog (SmoothieOptionsPanel.mm) actually calls it.

#ifndef __SMOOTHIESUITES_H__
#define __SMOOTHIESUITES_H__

#include "IllustratorSDK.h"
#include "Suites.hpp"
#include "AIStringFormatUtils.h"
#include "AIUITheme.h"

extern	"C"	AILiveEffectSuite*		sAILiveEffect;
extern	"C"	AIDictionarySuite*		sAIDictionary;
extern	"C"	AIMenuSuite*			sAIMenu;
extern	"C"	AIPathSuite*			sAIPath;
extern	"C"	AIRealMathSuite*		sAIRealMath;
extern	"C"	AIArtSuite*				sAIArt;
extern	"C"	AIUnicodeStringSuite*	sAIUnicodeString;
extern	"C"	AIStringFormatUtilsSuite*	sAIStringFormatUtils;
extern	"C"	SPBlocksSuite*			sSPBlocks;
// Native dialog theme colors (SmoothieOptionsPanel.mm). sAIUser (unit-
// aware number formatting, if we add that polish later) is already
// declared by the shared common/includes/Suites.hpp, pulled in
// transitively above.
extern	"C"	AIUIThemeSuite*			sAIUITheme;

#endif // End SmoothieSuites.h

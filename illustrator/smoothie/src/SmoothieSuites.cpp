// SmoothieSuites.cpp
// Adapted from the SDK's TwirlFilterSuites.cpp (samplecode/TwirlFilter/Source/TwirlFilterSuites.cpp).
// See SmoothieSuites.h for the 2026-09-15 trim -- and for why
// sAIUnicodeString/sAIStringFormatUtils/sSPBlocks are still here despite
// nothing in Source/*.cpp calling them directly (the Shared group's
// IAIUnicodeString.cpp/IAIStringFormatUtils.cpp need them at link time).

#include "IllustratorSDK.h"
#include "SmoothieSuites.h"

extern "C"
{
	AIMenuSuite*			sAIMenu = NULL;
	AILiveEffectSuite*		sAILiveEffect = NULL;
	AIDictionarySuite*		sAIDictionary = NULL;
	AIPathSuite*			sAIPath = NULL;
	AIRealMathSuite*		sAIRealMath = NULL;
	AIArtSuite*				sAIArt = NULL;
	AIUnicodeStringSuite*	sAIUnicodeString = NULL;
	AIStringFormatUtilsSuite*	sAIStringFormatUtils = NULL;
	SPBlocksSuite*			sSPBlocks = NULL;
	AIUIThemeSuite*			sAIUITheme = NULL;
};

ImportSuite gImportSuites[] =
{
	kAIMenuSuite, kAIMenuSuiteVersion, &sAIMenu,
	kAILiveEffectSuite, kAILiveEffectVersion, &sAILiveEffect,
	kAIDictionarySuite, kAIDictionaryVersion, &sAIDictionary,
	kAIPathSuite, kAIPathSuiteVersion, &sAIPath,
	kAIRealMathSuite, kAIRealMathVersion, &sAIRealMath,
	kAIArtSuite, kAIArtVersion, &sAIArt,
	kAIUnicodeStringSuite, kAIUnicodeStringSuiteVersion, &sAIUnicodeString,
	kAIStringFormatUtilsSuite, kAIStringFormatUtilsSuiteVersion, &sAIStringFormatUtils,
	kSPBlocksSuite, kSPBlocksSuiteVersion, &sSPBlocks,
	kAIUIThemeSuite, kAIUIThemeVersion, &sAIUITheme,
	nullptr, 0, nullptr
};

// End SmoothieSuites.cpp

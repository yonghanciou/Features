// KakuSuites.cpp
// Same shape as Smoothie's SmoothieSuites.cpp.

#include "IllustratorSDK.h"
#include "KakuSuites.h"

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
	AIPathStyleSuite*		sAIPathStyle = NULL;
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
	kAIPathStyleSuite, kAIPathStyleVersion, &sAIPathStyle,
	nullptr, 0, nullptr
};

// End KakuSuites.cpp

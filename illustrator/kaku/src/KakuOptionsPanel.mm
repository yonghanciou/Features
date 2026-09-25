// KakuOptionsPanel.mm
//
// Plain native AppKit modal dialog, built the same way as this developer's
// existing Smoothie plugin's SmoothieOptionsPanel.mm (see that file for the
// two layout bugs it already worked around -- no NSBox grouping, don't
// remove NSWindowStyleMaskClosable to hide the traffic lights -- both
// avoided here too since this dialog uses the exact same NSStackView shape).
//
// Deliberately small: fixed-pt cell sizing, the document-origin choice, the
// simplify-collinear toggle, and (later) the entire 邊緣吸附 mode were all
// removed at the user's request. Grid sizing is ratio-only now (格數),
// origin is always the selection's own top-left, and every path always
// simplifies collinear runs. 像素化 is listed first and is the default
// mode. 多邊形 is a second mode (not grid-based -- see KakuMath.h's
// PolygonizeSegments): it uses 面數 instead of 格數/取樣密度, so those rows
// get disabled rather than hidden while it's selected (updateModeEnabled).
//
// Window content height is computed from the stack view's own `fittingSize`
// after it's built (see the earlier phase-2 revision of this file for why:
// no way to eyeball-tune a pixel constant in this environment, unlike
// Smoothie's hand-tuned `168`).
//
// This file must be compiled with -fobjc-arc (set as a per-file compiler
// flag in Kaku.xcodeproj, same as Smoothie's -- the project as a
// whole is not ARC by default).

#include "KakuOptionsPanel.h"

#if KAKU_HAVE_AI_SDK

#include "KakuSuites.h" // sAILiveEffect, sAIUITheme
#include "KakuEffectParams.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cmath>

using namespace kaku;

namespace {

NSColor* ThemeColor(ai::int32 componentColor, NSColor* fallback) {
    AIUIThemeColor c;
    ASErr err = sAIUITheme->GetUIThemeColor(kAIUIThemeSelectorDialog, componentColor, c);
    if (err) return fallback;
    return [NSColor colorWithSRGBRed:c.red green:c.green blue:c.blue alpha:c.alpha];
}

} // namespace

// ---------------------------------------------------------------------
// Controller: owns the window + controls for one dialog session. Plain
// target/action wiring, same as SmoothieDialogController.
// ---------------------------------------------------------------------
@interface KakuDialogController : NSObject <NSTextFieldDelegate>

@property (nonatomic, strong) NSWindow* window;
@property (nonatomic, strong) NSButton* modeBlockRadio;
@property (nonatomic, strong) NSButton* modePolygonRadio;
@property (nonatomic, strong) NSSlider* ratioSlider;
@property (nonatomic, strong) NSTextField* ratioField;
@property (nonatomic, strong) NSSlider* densitySlider;
@property (nonatomic, strong) NSTextField* densityField;
@property (nonatomic, strong) NSSlider* facetsSlider;
@property (nonatomic, strong) NSTextField* facetsField;
@property (nonatomic, strong) NSButton* previewCheckbox;

@property (nonatomic, assign) AILiveEffectParameters aiParameters;
@property (nonatomic, assign) AILiveEffectParamContext aiContext;
@property (nonatomic, assign) BOOL allowPreview;

- (instancetype)initWithInitialParams:(const GridParams&)initial allowPreview:(BOOL)allowPreview;
- (GridParams)currentParams;
- (void)controlChanged:(id)sender;
- (void)okClicked:(id)sender;
- (void)cancelClicked:(id)sender;

@end

@implementation KakuDialogController {
    NSModalResponse _response;
}

- (instancetype)initWithInitialParams:(const GridParams&)initial allowPreview:(BOOL)allowPreview {
    self = [super init];
    if (!self) return self;
    self.allowPreview = allowPreview;

    const CGFloat kWinWidth = 320;
    const CGFloat kPad = 16;

    BOOL dark = sAIUITheme->IsUIThemeDark();
    NSColor* textColor = ThemeColor(kAIUIComponentColorText, [NSColor labelColor]);

    // --- 模式 radios -- own container: AppKit's NSButtonTypeRadio
    // auto-exclusivity groups by "same superview", so this pair needs its
    // own stack rather than sharing one with any other radio group.
    // 像素化 listed (and defaulted to) first, per the user's choice. 多邊形
    // is a second, unrelated algorithm (not grid-based -- see
    // PolygonizeSegments in KakuMath.h): it uses `facets` instead of
    // `格數`/`取樣密度`, so those two rows get disabled while it's selected
    // (updateModeEnabled below) rather than removed, to avoid re-measuring
    // and resizing the window on every mode switch. (A third mode, 邊緣吸附,
    // existed earlier and was removed entirely at the user's request.)
    NSTextField* modeLabel = [NSTextField labelWithString:@"模式:"];
    modeLabel.textColor = textColor;

    self.modeBlockRadio = [NSButton radioButtonWithTitle:@"像素化" target:self action:@selector(controlChanged:)];
    self.modePolygonRadio = [NSButton radioButtonWithTitle:@"多邊形" target:self action:@selector(controlChanged:)];
    self.modeBlockRadio.state = (initial.mode == PixelateMode::kBlock) ? NSControlStateValueOn : NSControlStateValueOff;
    self.modePolygonRadio.state = (initial.mode == PixelateMode::kPolygon) ? NSControlStateValueOn : NSControlStateValueOff;

    NSStackView* modeStack = [NSStackView stackViewWithViews:@[self.modeBlockRadio, self.modePolygonRadio]];
    modeStack.orientation = NSUserInterfaceLayoutOrientationVertical;
    modeStack.alignment = NSLayoutAttributeLeading;
    modeStack.spacing = 4;

    // --- 跨向格數 row (the only cell-sizing control) ---
    NSTextField* ratioLabel = [NSTextField labelWithString:@"格數:"];
    ratioLabel.textColor = textColor;

    self.ratioSlider = [NSSlider sliderWithValue:initial.cellRatio
                                         minValue:1.0
                                         maxValue:200.0
                                           target:self
                                           action:@selector(controlChanged:)];
    [self.ratioSlider setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];

    self.ratioField = [NSTextField textFieldWithString:[NSString stringWithFormat:@"%.0f", initial.cellRatio]];
    self.ratioField.delegate = self;
    self.ratioField.target = self;
    self.ratioField.action = @selector(controlChanged:);
    self.ratioField.alignment = NSTextAlignmentRight;
    [self.ratioField.widthAnchor constraintEqualToConstant:36].active = YES;

    NSStackView* ratioRow = [NSStackView stackViewWithViews:@[ratioLabel, self.ratioSlider, self.ratioField]];
    ratioRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    ratioRow.distribution = NSStackViewDistributionFill;

    // --- 取樣密度 row ---
    NSTextField* densityLabel = [NSTextField labelWithString:@"取樣密度:"];
    densityLabel.textColor = textColor;

    self.densitySlider = [NSSlider sliderWithValue:initial.density
                                           minValue:1.0
                                           maxValue:20.0
                                             target:self
                                             action:@selector(controlChanged:)];
    [self.densitySlider setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];

    self.densityField = [NSTextField textFieldWithString:[NSString stringWithFormat:@"%.0f", initial.density]];
    self.densityField.delegate = self;
    self.densityField.target = self;
    self.densityField.action = @selector(controlChanged:);
    self.densityField.alignment = NSTextAlignmentRight;
    [self.densityField.widthAnchor constraintEqualToConstant:36].active = YES;

    NSStackView* densityRow = [NSStackView stackViewWithViews:@[self.densitySlider, self.densityField]];
    densityRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    densityRow.distribution = NSStackViewDistributionFill;

    // --- 面數 row (多邊形 only) ---
    NSTextField* facetsLabel = [NSTextField labelWithString:@"面數:"];
    facetsLabel.textColor = textColor;

    self.facetsSlider = [NSSlider sliderWithValue:initial.facets
                                          minValue:1.0
                                          maxValue:6.0
                                            target:self
                                            action:@selector(controlChanged:)];
    [self.facetsSlider setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];

    self.facetsField = [NSTextField textFieldWithString:[NSString stringWithFormat:@"%d", initial.facets]];
    self.facetsField.delegate = self;
    self.facetsField.target = self;
    self.facetsField.action = @selector(controlChanged:);
    self.facetsField.alignment = NSTextAlignmentRight;
    [self.facetsField.widthAnchor constraintEqualToConstant:36].active = YES;

    NSStackView* facetsRow = [NSStackView stackViewWithViews:@[facetsLabel, self.facetsSlider, self.facetsField]];
    facetsRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    facetsRow.distribution = NSStackViewDistributionFill;

    // --- Preview checkbox ---
    self.previewCheckbox = [NSButton checkboxWithTitle:@"預覽" target:self action:@selector(controlChanged:)];
    self.previewCheckbox.state = allowPreview ? NSControlStateValueOn : NSControlStateValueOff;
    self.previewCheckbox.enabled = allowPreview;

    // --- Buttons row ---
    NSButton* cancelButton = [NSButton buttonWithTitle:@"取消" target:self action:@selector(cancelClicked:)];
    cancelButton.bezelStyle = NSBezelStyleRounded;
    cancelButton.keyEquivalent = @"\033"; // Escape
    NSButton* okButton = [NSButton buttonWithTitle:@"確定" target:self action:@selector(okClicked:)];
    okButton.bezelStyle = NSBezelStyleRounded;
    // Deliberately no "\r" keyEquivalent -- see Smoothie's file header for why.
    NSStackView* buttonRow = [NSStackView stackViewWithViews:@[cancelButton, okButton]];
    buttonRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    buttonRow.alignment = NSLayoutAttributeCenterY;
    buttonRow.distribution = NSStackViewDistributionEqualSpacing;
    buttonRow.translatesAutoresizingMaskIntoConstraints = NO;

    // --- Flat vertical stack of rows (no NSBox -- see file header) ---
    NSStackView* rootStack = [NSStackView stackViewWithViews:@[
        modeLabel, modeStack,
        ratioRow,
        densityLabel, densityRow,
        facetsRow,
        self.previewCheckbox, buttonRow
    ]];
    rootStack.orientation = NSUserInterfaceLayoutOrientationVertical;
    rootStack.alignment = NSLayoutAttributeLeading;
    rootStack.spacing = 12;
    rootStack.translatesAutoresizingMaskIntoConstraints = NO;
    rootStack.frame = NSMakeRect(0, 0, kWinWidth - 2 * kPad, 0);

    // Measure the stack's own natural height (no hand-tuned constant --
    // see file header) before building the window around it.
    [rootStack layoutSubtreeIfNeeded];
    CGFloat contentHeight = rootStack.fittingSize.height;
    CGFloat winHeight = contentHeight + 2 * kPad;

    NSRect frame = NSMakeRect(0, 0, kWinWidth, winHeight);
    NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                     styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                                                       backing:NSBackingStoreBuffered
                                                         defer:NO];
    window.title = @"Kaku";
    window.releasedWhenClosed = NO;

    // Hide the traffic lights without touching styleMask (see file header).
    [[window standardWindowButton:NSWindowCloseButton] setHidden:YES];
    [[window standardWindowButton:NSWindowMiniaturizeButton] setHidden:YES];
    [[window standardWindowButton:NSWindowZoomButton] setHidden:YES];

    window.appearance = [NSAppearance appearanceNamed:(dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua)];
    window.backgroundColor = ThemeColor(kAIUIComponentColorBackground, [NSColor windowBackgroundColor]);

    self.window = window;

    NSView* content = window.contentView;
    [content addSubview:rootStack];
    [NSLayoutConstraint activateConstraints:@[
        [rootStack.topAnchor constraintEqualToAnchor:content.topAnchor constant:kPad],
        [rootStack.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:kPad],
        [rootStack.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-kPad],
        [rootStack.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-kPad],
        [buttonRow.widthAnchor constraintEqualToAnchor:rootStack.widthAnchor],
    ]];

    [self updateModeEnabled];

    return self;
}

// 多邊形模式下格數/取樣密度用不到(不是網格演算法),面數只有多邊形用得到 --
// 灰掉不相關的欄位,而不是整排藏起來,避免每次切模式都要重新量視窗高度。
- (void)updateModeEnabled {
    BOOL polygon = (self.modePolygonRadio.state == NSControlStateValueOn);
    self.ratioSlider.enabled = !polygon;
    self.ratioField.enabled = !polygon;
    self.densitySlider.enabled = !polygon;
    self.densityField.enabled = !polygon;
    self.facetsSlider.enabled = polygon;
    self.facetsField.enabled = polygon;
}

- (GridParams)currentParams {
    GridParams p;
    p.mode = (self.modePolygonRadio.state == NSControlStateValueOn) ? PixelateMode::kPolygon : PixelateMode::kBlock;
    p.cellRatio = self.ratioSlider.doubleValue;
    p.density = self.densitySlider.doubleValue;
    p.facets = (int)std::round(self.facetsSlider.doubleValue);
    return p;
}

- (void)syncRatioFieldFromSlider {
    self.ratioField.stringValue = [NSString stringWithFormat:@"%.0f", self.ratioSlider.doubleValue];
}

- (void)syncSliderFromRatioField {
    double v = self.ratioField.doubleValue;
    if (v < 1) v = 1;
    if (v > 200) v = 200;
    self.ratioSlider.doubleValue = v;
}

- (void)syncDensityFieldFromSlider {
    self.densityField.stringValue = [NSString stringWithFormat:@"%.0f", self.densitySlider.doubleValue];
}

- (void)syncSliderFromDensityField {
    double v = self.densityField.doubleValue;
    if (v < 1) v = 1;
    if (v > 20) v = 20;
    self.densitySlider.doubleValue = v;
}

- (void)syncFacetsFieldFromSlider {
    self.facetsField.stringValue = [NSString stringWithFormat:@"%.0f", self.facetsSlider.doubleValue];
}

- (void)syncSliderFromFacetsField {
    double v = self.facetsField.doubleValue;
    if (v < 1) v = 1;
    if (v > 6) v = 6;
    self.facetsSlider.doubleValue = v;
}

- (void)controlChanged:(id)sender {
    if (sender == self.ratioSlider) {
        [self syncRatioFieldFromSlider];
    } else if (sender == self.ratioField) {
        [self syncSliderFromRatioField];
    } else if (sender == self.densitySlider) {
        [self syncDensityFieldFromSlider];
    } else if (sender == self.densityField) {
        [self syncSliderFromDensityField];
    } else if (sender == self.facetsSlider) {
        [self syncFacetsFieldFromSlider];
    } else if (sender == self.facetsField) {
        [self syncSliderFromFacetsField];
    } else if (sender == self.modeBlockRadio || sender == self.modePolygonRadio) {
        [self updateModeEnabled];
    }

    if (self.previewCheckbox.state == NSControlStateValueOn && self.allowPreview) {
        GridParams p = [self currentParams];
        WriteParams(self.aiParameters, p);
        sAILiveEffect->UpdateParameters(self.aiContext);
    }
}

- (void)okClicked:(id)sender {
    (void)sender;
    _response = NSModalResponseOK;
    [NSApp stopModalWithCode:NSModalResponseOK];
}

- (void)cancelClicked:(id)sender {
    (void)sender;
    _response = NSModalResponseCancel;
    [NSApp stopModalWithCode:NSModalResponseCancel];
}

// NSTextFieldDelegate: live-update as the user types, not just on commit.
- (void)controlTextDidChange:(NSNotification*)notification {
    [self controlChanged:notification.object];
}

// Arrow-key increment, same interception point as Smoothie's dialog (the
// field editor calls this before running standard editing commands) --
// see SmoothieOptionsPanel.mm's file header for why a subclassed
// -keyDown: does not work here.
- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)commandSelector {
    (void)textView;
    BOOL isUp;
    double step;
    if (commandSelector == @selector(moveUp:)) { isUp = YES; step = 1.0; }
    else if (commandSelector == @selector(moveDown:)) { isUp = NO; step = 1.0; }
    else if (commandSelector == @selector(moveUpAndModifySelection:)) { isUp = YES; step = 10.0; }
    else if (commandSelector == @selector(moveDownAndModifySelection:)) { isUp = NO; step = 10.0; }
    else return NO;

    double delta = isUp ? step : -step;

    if (control == self.ratioField) {
        double v = std::min(200.0, std::max(1.0, self.ratioSlider.doubleValue + delta));
        self.ratioSlider.doubleValue = v;
        [self syncRatioFieldFromSlider];
        [self controlChanged:self.ratioSlider];
        return YES;
    }
    if (control == self.densityField) {
        double v = std::min(20.0, std::max(1.0, self.densitySlider.doubleValue + delta));
        self.densitySlider.doubleValue = v;
        [self syncDensityFieldFromSlider];
        [self controlChanged:self.densitySlider];
        return YES;
    }
    if (control == self.facetsField) {
        double v = std::min(6.0, std::max(1.0, self.facetsSlider.doubleValue + delta));
        self.facetsSlider.doubleValue = v;
        [self syncFacetsFieldFromSlider];
        [self controlChanged:self.facetsSlider];
        return YES;
    }
    return NO;
}

@end

namespace kaku {

bool ShowOptionsDialog(AILiveEffectParameters parameters,
                        AILiveEffectParamContext context,
                        const GridParams& initial,
                        bool allowPreview,
                        GridParams* outParams) {
    @autoreleasepool {
        KakuDialogController* controller =
            [[KakuDialogController alloc] initWithInitialParams:initial allowPreview:allowPreview];
        controller.aiParameters = parameters;
        controller.aiContext = context;

        [controller.window center];
        [controller.window makeKeyAndOrderFront:nil];

        NSModalResponse response = [NSApp runModalForWindow:controller.window];
        [controller.window orderOut:nil];

        if (response == NSModalResponseOK) {
            if (outParams) *outParams = [controller currentParams];
            return true;
        }
        return false;
    }
}

} // namespace kaku

#endif // KAKU_HAVE_AI_SDK

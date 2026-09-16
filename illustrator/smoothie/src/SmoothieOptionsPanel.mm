// SmoothieOptionsPanel.mm
//
// Plain native AppKit modal dialog. See SmoothieOptionsPanel.h for why
// this replaced the CSXS panel. Layout notes carried over from the
// earlier (deleted) attempt at this same dialog, so we don't re-discover
// the same two bugs:
//   - No NSBox grouping. Wrapping controls in an NSBox previously made the
//     enclosing NSStackView compute the wrong height (NSBox's contentView
//     doesn't participate in Auto Layout sizing like a normal arranged
//     subview does) -- rows go straight into one flat vertical
//     NSStackView instead.
//   - Don't remove NSWindowStyleMaskClosable to hide the traffic lights
//     (changes title-bar height calculations and breaks layout). Hide the
//     three button views individually instead.
//
// This file must be compiled with -fobjc-arc (set as a per-file compiler
// flag on this file in Smoothie.xcodeproj, since the project as a whole
// is not ARC by default) -- the earlier version leaked a small amount of
// memory per dialog open before that flag was added.

#include "SmoothieOptionsPanel.h"

#if SMOOTHIE_HAVE_AI_SDK

#include "SmoothieSuites.h" // sAILiveEffect, sAIUITheme
#include "CornerEffectParams.h"
#include "IAIUnicodeString.h" // ai::UnicodeString::FromUTF8 / as_UTF8

#import <Cocoa/Cocoa.h>

#include <algorithm>

using namespace smoothie;

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
// target/action wiring, no bindings/KVO -- keeps this easy to reason
// about for a first rewrite.
//
// Arrow-key increment note: an earlier revision tried to get Up/Down
// working by subclassing NSTextField and overriding -keyDown: -- that
// never fired, because once a text field starts being edited, AppKit
// swaps in the window's shared *field editor* (an NSTextView) as first
// responder; -keyDown: on the NSTextField instance itself is bypassed
// entirely. The correct, documented interception point is the control's
// delegate method -control:textView:doCommandBySelector:, which the field
// editor calls before running standard editing commands like moveUp:/
// moveDown: -- implemented below on the controller (already the
// delegate for both text fields) instead of on a subclass.
// ---------------------------------------------------------------------
@interface SmoothieDialogController : NSObject <NSTextFieldDelegate>

@property (nonatomic, strong) NSTextField* radiusField;
@property (nonatomic, strong) NSWindow* window;
@property (nonatomic, strong) NSStepper* radiusStepper;
@property (nonatomic, strong) NSSlider* curvatureSlider;
@property (nonatomic, strong) NSTextField* curvatureField;
@property (nonatomic, strong) NSButton* previewCheckbox;

@property (nonatomic, assign) AILiveEffectParameters aiParameters;
@property (nonatomic, assign) AILiveEffectParamContext aiContext;
@property (nonatomic, assign) BOOL allowPreview;

- (instancetype)initWithInitialParams:(const CornerParams&)initial allowPreview:(BOOL)allowPreview;
- (CornerParams)currentParams;
- (void)controlChanged:(id)sender;
- (void)okClicked:(id)sender;
- (void)cancelClicked:(id)sender;

@end

@implementation SmoothieDialogController {
    NSModalResponse _response;
}

- (instancetype)initWithInitialParams:(const CornerParams&)initial allowPreview:(BOOL)allowPreview {
    self = [super init];
    if (!self) return self;
    self.allowPreview = allowPreview;

    const CGFloat kWinWidth = 300;
    const CGFloat kPad = 16;
    NSRect frame = NSMakeRect(0, 0, kWinWidth, 168);

    NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                     styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                                                       backing:NSBackingStoreBuffered
                                                         defer:NO];
    window.title = @"Smoothie";
    window.releasedWhenClosed = NO;

    // Hide the traffic lights without touching styleMask (see file header).
    [[window standardWindowButton:NSWindowCloseButton] setHidden:YES];
    [[window standardWindowButton:NSWindowMiniaturizeButton] setHidden:YES];
    [[window standardWindowButton:NSWindowZoomButton] setHidden:YES];

    BOOL dark = sAIUITheme->IsUIThemeDark();
    window.appearance = [NSAppearance appearanceNamed:(dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua)];
    window.backgroundColor = ThemeColor(kAIUIComponentColorBackground, dark ? [NSColor windowBackgroundColor] : [NSColor windowBackgroundColor]);
    NSColor* textColor = ThemeColor(kAIUIComponentColorText, [NSColor labelColor]);

    self.window = window;

    // --- Radius row ---
    NSTextField* radiusLabel = [NSTextField labelWithString:@"半徑:"];
    radiusLabel.textColor = textColor;

    self.radiusField = [NSTextField textFieldWithString:@""];
    self.radiusField.delegate = self;
    self.radiusField.target = self;
    self.radiusField.action = @selector(controlChanged:);
    [self.radiusField setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    [self setRadiusPoints:initial.radius]; // formats using the document's current ruler unit

    self.radiusStepper = [NSStepper new];
    self.radiusStepper.minValue = 0.0;
    self.radiusStepper.maxValue = 100000.0;
    self.radiusStepper.increment = 1.0;
    self.radiusStepper.valueWraps = NO;
    self.radiusStepper.doubleValue = initial.radius;
    self.radiusStepper.target = self;
    self.radiusStepper.action = @selector(controlChanged:);

    NSStackView* radiusRow = [NSStackView stackViewWithViews:@[radiusLabel, self.radiusField, self.radiusStepper]];
    radiusRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    radiusRow.distribution = NSStackViewDistributionFill;

    // --- Curvature row ---
    NSTextField* curvatureLabel = [NSTextField labelWithString:@"曲率:"];
    curvatureLabel.textColor = textColor;

    self.curvatureSlider = [NSSlider sliderWithValue:initial.curvaturePercent * 100.0
                                             minValue:0.0
                                             maxValue:100.0
                                               target:self
                                               action:@selector(controlChanged:)];
    [self.curvatureSlider setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];

    self.curvatureField = [NSTextField textFieldWithString:[NSString stringWithFormat:@"%.0f%%", initial.curvaturePercent * 100.0]];
    self.curvatureField.delegate = self;
    self.curvatureField.target = self;
    self.curvatureField.action = @selector(controlChanged:);
    self.curvatureField.alignment = NSTextAlignmentRight;
    [self.curvatureField.widthAnchor constraintEqualToConstant:52].active = YES;

    NSStackView* curvatureRow = [NSStackView stackViewWithViews:@[self.curvatureSlider, self.curvatureField]];
    curvatureRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    curvatureRow.distribution = NSStackViewDistributionFill;

    // --- Preview checkbox ---
    self.previewCheckbox = [NSButton checkboxWithTitle:@"預視" target:self action:@selector(controlChanged:)];
    self.previewCheckbox.state = allowPreview ? NSControlStateValueOn : NSControlStateValueOff;
    self.previewCheckbox.enabled = allowPreview;

    // --- Buttons row ---
    NSButton* cancelButton = [NSButton buttonWithTitle:@"取消" target:self action:@selector(cancelClicked:)];
    cancelButton.bezelStyle = NSBezelStyleRounded;
    cancelButton.keyEquivalent = @"\033"; // Escape
    NSButton* okButton = [NSButton buttonWithTitle:@"確定" target:self action:@selector(okClicked:)];
    okButton.bezelStyle = NSBezelStyleRounded;
    // Deliberately no "\r" keyEquivalent: AppKit auto-tints the default
    // (Return-triggered) button blue, which is exactly the single
    // eye-catching accent color Illustrator's own dialogs avoid for a
    // plain OK/Cancel pair.
    NSStackView* buttonRow = [NSStackView stackViewWithViews:@[cancelButton, okButton]];
    buttonRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    buttonRow.alignment = NSLayoutAttributeCenterY;
    buttonRow.distribution = NSStackViewDistributionEqualSpacing;

    // --- Flat vertical stack of rows (no NSBox -- see file header) ---
    NSStackView* rootStack = [NSStackView stackViewWithViews:@[radiusRow, curvatureLabel, curvatureRow, self.previewCheckbox, buttonRow]];
    rootStack.orientation = NSUserInterfaceLayoutOrientationVertical;
    rootStack.alignment = NSLayoutAttributeLeading;
    rootStack.spacing = 12;
    rootStack.translatesAutoresizingMaskIntoConstraints = NO;
    buttonRow.translatesAutoresizingMaskIntoConstraints = NO;

    NSView* content = window.contentView;
    [content addSubview:rootStack];
    [NSLayoutConstraint activateConstraints:@[
        [rootStack.topAnchor constraintEqualToAnchor:content.topAnchor constant:kPad],
        [rootStack.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:kPad],
        [rootStack.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-kPad],
        [rootStack.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-kPad],
        [buttonRow.widthAnchor constraintEqualToAnchor:rootStack.widthAnchor],
    ]];

    return self;
}

// Radius is stored internally in points (document units, same as every
// other AIReal coordinate) but shown/typed in whatever unit the document's
// rulers are currently set to (mm/pt/in/pica/cm/px) -- the exact function
// pair Illustrator's own built-in Round Corners dialog uses for its radius
// field (IUAIRealToStringUnitsWithoutScale / IUStringUnitsToAIRealWithoutScale).
- (double)radiusPoints {
    ai::UnicodeString input = ai::UnicodeString::FromUTF8([self.radiusField.stringValue UTF8String]);
    AIReal value = 0;
    sAIUser->IUStringUnitsToAIRealWithoutScale(input, &value);
    return value;
}

- (void)setRadiusPoints:(double)points {
    ai::UnicodeString formatted;
    sAIUser->IUAIRealToStringUnitsWithoutScale((AIReal)points, 2, formatted);
    self.radiusField.stringValue = [NSString stringWithUTF8String:formatted.as_UTF8().c_str()];
}

- (CornerParams)currentParams {
    CornerParams p;
    p.radius = [self radiusPoints];
    double pct = self.curvatureSlider.doubleValue;
    p.curvaturePercent = pct / 100.0;
    return p;
}

- (void)syncCurvatureFieldFromSlider {
    self.curvatureField.stringValue = [NSString stringWithFormat:@"%.0f%%", self.curvatureSlider.doubleValue];
}

- (void)syncSliderFromCurvatureField {
    double v = self.curvatureField.doubleValue; // stringValue like "67%" -> doubleValue parses the leading "67"
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    self.curvatureSlider.doubleValue = v;
}

- (void)controlChanged:(id)sender {
    if (sender == self.radiusStepper) {
        [self setRadiusPoints:self.radiusStepper.doubleValue];
    } else if (sender == self.radiusField) {
        self.radiusStepper.doubleValue = [self radiusPoints];
    } else if (sender == self.curvatureSlider) {
        [self syncCurvatureFieldFromSlider];
    } else if (sender == self.curvatureField) {
        [self syncSliderFromCurvatureField];
    }

    if (self.previewCheckbox.state == NSControlStateValueOn && self.allowPreview) {
        CornerParams p = [self currentParams];
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

// NSControlTextEditingDelegate: the field editor calls this before running
// a standard editing command -- Up/Down arrow map to moveUp:/moveDown:,
// Shift+Up/Down map to the "AndModifySelection" variants (Shift normally
// extends a text selection; there's nothing to select in a single-line
// numeric field, so repurposing it as a x10 step is safe here and matches
// Illustrator's own numeric fields). Returning YES tells the field editor
// "handled, don't do your default thing" (which for these selectors in a
// single-line field would do nothing visible anyway).
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

    if (control == self.radiusField) {
        double v = std::max(0.0, [self radiusPoints] + delta);
        [self setRadiusPoints:v];
        self.radiusStepper.doubleValue = v;
        [self controlChanged:self.radiusField];
        return YES;
    }
    if (control == self.curvatureField) {
        double v = std::min(100.0, std::max(0.0, self.curvatureSlider.doubleValue + delta));
        self.curvatureSlider.doubleValue = v;
        [self syncCurvatureFieldFromSlider];
        [self controlChanged:self.curvatureSlider];
        return YES;
    }
    return NO;
}

@end

namespace smoothie {

bool ShowOptionsDialog(AILiveEffectParameters parameters,
                        AILiveEffectParamContext context,
                        const CornerParams& initial,
                        bool allowPreview,
                        CornerParams* outParams) {
    @autoreleasepool {
        SmoothieDialogController* controller =
            [[SmoothieDialogController alloc] initWithInitialParams:initial allowPreview:allowPreview];
        controller.aiParameters = parameters;
        controller.aiContext = context;

        [controller.window center];
        [controller.window makeKeyAndOrderFront:nil];

        // Synchronous, same call frame the whole time -- `parameters`/
        // `context` are only ever touched while this function is on the
        // stack, same as the pre-CSXS version that never crashed.
        NSModalResponse response = [NSApp runModalForWindow:controller.window];
        [controller.window orderOut:nil];

        if (response == NSModalResponseOK) {
            if (outParams) *outParams = [controller currentParams];
            return true;
        }
        return false;
    }
}

} // namespace smoothie

#endif // SMOOTHIE_HAVE_AI_SDK

//
//  TerminalView.m
//  iSH
//
//  Created by Theodore Dubois on 11/3/17.
//

#import "TerminalView.h"
#import "UserPreferences.h"
#import "ScrollbarView.h"
#import "NSObject+SaneKVO.h"
#import "Theme.h"
#import <ISHInstrumentation.h>

@class TerminalView;

@interface TerminalTextPosition : UITextPosition
@property (nonatomic) NSInteger offset;
+ (instancetype)positionWithOffset:(NSInteger)offset;
@end

@implementation TerminalTextPosition
+ (instancetype)positionWithOffset:(NSInteger)offset {
    TerminalTextPosition *position = [TerminalTextPosition new];
    position.offset = offset;
    return position;
}
@end

@interface TerminalTextRange : UITextRange
@property (nonatomic) TerminalTextPosition *startPosition;
@property (nonatomic) TerminalTextPosition *endPosition;
+ (instancetype)rangeWithStart:(NSInteger)start end:(NSInteger)end;
@end

@implementation TerminalTextRange
+ (instancetype)rangeWithStart:(NSInteger)start end:(NSInteger)end {
    TerminalTextRange *range = [TerminalTextRange new];
    range.startPosition = [TerminalTextPosition positionWithOffset:start];
    range.endPosition = [TerminalTextPosition positionWithOffset:end];
    return range;
}

- (UITextPosition *)start {
    return self.startPosition;
}

- (UITextPosition *)end {
    return self.endPosition;
}

- (BOOL)isEmpty {
    return self.startPosition.offset == self.endPosition.offset;
}
@end

@interface TerminalSurfaceAccessibilityElement : UIAccessibilityElement
@property (nonatomic, weak) TerminalView *terminalView;
@end

@implementation TerminalSurfaceAccessibilityElement
- (BOOL)accessibilityActivate {
    [self.terminalView focusForTesting];
    return YES;
}
@end

struct rowcol {
    int row;
    int col;
};

@interface TerminalView () <UITextFieldDelegate>

@property UITapGestureRecognizer *tapRecognizer;
@property (nonatomic) NSMutableArray<UIKeyCommand *> *keyCommands;
@property ScrollbarView *scrollbarView;
@property (nonatomic) BOOL terminalFocused;
@property (nonatomic) UITextField *inputBridgeField;
@property (nonatomic) BOOL usingInputBridge;

@property (nullable) NSString *markedText;
@property (nullable) NSString *selectedText;
@property UITextRange *markedRange;
@property UITextRange *selectedRange;
@property (nonatomic) BOOL suppressNextMarkedTextCommit;

@property struct rowcol floatingCursor;
@property CGSize floatingCursorSensitivity;
@property CGSize actualFloatingCursorSensitivity;
@property (nonatomic) BOOL didCommonInit;

@end

@implementation TerminalView {
    TerminalSurfaceAccessibilityElement *_terminalAccessibilityElement;
}

@synthesize inputAccessoryView = _inputAccessoryView;

- (BOOL)isRunningUITests {
    NSDictionary *environment = NSProcessInfo.processInfo.environment;
    return environment[@"XCTestConfigurationFilePath"] != nil || environment[@"IXLAND_UI_TESTING"] != nil;
}

- (UITextField *)uiTestInputField {
    return self.inputBridgeField;
}

- (TerminalTextPosition *)terminalDocumentPosition {
    return [TerminalTextPosition positionWithOffset:0];
}

- (TerminalTextRange *)terminalEmptyRange {
    return [TerminalTextRange rangeWithStart:0 end:0];
}

- (TerminalTextPosition *)terminalPositionFromUITextPosition:(UITextPosition *)position {
    if ([position isKindOfClass:[TerminalTextPosition class]])
        return (TerminalTextPosition *) position;
    return [self terminalDocumentPosition];
}

- (TerminalTextRange *)terminalRangeFromUITextRange:(UITextRange *)range {
    if ([range isKindOfClass:[TerminalTextRange class]])
        return (TerminalTextRange *) range;
    return [self terminalEmptyRange];
}

- (UIAccessibilityElement *)terminalAccessibilityElement {
    if (!_terminalAccessibilityElement) {
        _terminalAccessibilityElement = [[TerminalSurfaceAccessibilityElement alloc] initWithAccessibilityContainer:self];
        _terminalAccessibilityElement.terminalView = self;
        _terminalAccessibilityElement.accessibilityIdentifier = @"TerminalSurface";
        _terminalAccessibilityElement.accessibilityLabel = @"Terminal";
        _terminalAccessibilityElement.accessibilityTraits = UIAccessibilityTraitAllowsDirectInteraction;
        _terminalAccessibilityElement.accessibilityFrameInContainerSpace = self.bounds;
        // Also set accessibilityFrame (screen coordinates) so XCUI snapshots
        // and queries can find the element reliably. Only do this when running
        // under XCTest to avoid affecting non-test behavior.
        BOOL isTesting = NSProcessInfo.processInfo.environment[@"XCTestConfigurationFilePath"] != nil;
        if (isTesting) {
            CGRect frameInScreen = CGRectZero;
            if (self.window != nil) {
                frameInScreen = [self.window convertRect:[self convertRect:self.bounds toView:self.window] toCoordinateSpace:UIScreen.mainScreen.coordinateSpace];
            }
            if (CGRectIsEmpty(frameInScreen)) {
                // Fallback to a reasonable rectangle based on the view bounds.
                frameInScreen = CGRectMake(20.0, 200.0, MAX(8.0, CGRectGetWidth(self.bounds)), MAX(8.0, CGRectGetHeight(self.bounds)));
            }
            _terminalAccessibilityElement.accessibilityFrame = frameInScreen;
        }
        _terminalAccessibilityElement.accessibilityValue = @"No terminal output";
    }
    return _terminalAccessibilityElement;
}

- (void)setTerminalAccessibilityElement:(UIAccessibilityElement *)terminalAccessibilityElement {
    _terminalAccessibilityElement = (TerminalSurfaceAccessibilityElement *)terminalAccessibilityElement;
    _terminalAccessibilityElement.terminalView = self;
}

@synthesize inputDelegate;
@synthesize tokenizer;

- (instancetype)initWithFrame:(CGRect)frame {
    if (self = [super initWithFrame:frame]) {
        [self commonInit];
    }
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder {
    if (self = [super initWithCoder:coder]) {
        [self commonInit];
    }
    return self;
}

- (void)commonInit {
    if (self.didCommonInit)
        return;
    self.didCommonInit = YES;

    self.usingInputBridge = [self isRunningUITests];
    if (self.usingInputBridge) {
        self.inputBridgeField = [[UITextField alloc] initWithFrame:CGRectMake(-100, -100, 1, 1)];
        self.inputBridgeField.accessibilityIdentifier = @"TerminalInput";
        self.inputBridgeField.autocorrectionType = UITextAutocorrectionTypeNo;
        self.inputBridgeField.spellCheckingType = UITextSpellCheckingTypeNo;
        self.inputBridgeField.smartDashesType = UITextSmartDashesTypeNo;
        self.inputBridgeField.smartInsertDeleteType = UITextSmartInsertDeleteTypeNo;
        self.inputBridgeField.smartQuotesType = UITextSmartQuotesTypeNo;
        self.inputBridgeField.returnKeyType = UIReturnKeyDefault;
        self.inputBridgeField.delegate = self;
        self.inputBridgeField.backgroundColor = UIColor.clearColor;
        self.inputBridgeField.textColor = UIColor.clearColor;
        self.inputBridgeField.tintColor = UIColor.clearColor;
        self.inputBridgeField.borderStyle = UITextBorderStyleNone;
        self.inputBridgeField.alpha = 0.01;
        [self addSubview:self.inputBridgeField];
    }
    self.inputAssistantItem.leadingBarButtonGroups = @[];
    self.inputAssistantItem.trailingBarButtonGroups = @[];

    ScrollbarView *scrollbarView = self.scrollbarView = [[ScrollbarView alloc] initWithFrame:self.bounds];
    scrollbarView.delegate = self;
    scrollbarView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    scrollbarView.bounces = NO;
    [self addSubview:scrollbarView];
    if (self.inputBridgeField != nil) {
        [self bringSubviewToFront:self.inputBridgeField];
    }

    UserPreferences *prefs = UserPreferences.shared;
    [prefs observe:@[@"capsLockMapping", @"optionMapping", @"backtickMapEscape", @"overrideControlSpace"]
           options:0 owner:self usingBlock:^(typeof(self) self) {
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_keyCommands = nil;
        });
    }];
    [prefs observe:@[@"colorScheme", @"fontFamily", @"fontSize", @"theme", @"cursorStyle", @"blinkCursor"]
           options:0 owner:self usingBlock:^(typeof(self) self) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self _updateStyle];
        });
    }];

    self.markedRange = [self terminalEmptyRange];
    self.selectedRange = [self terminalEmptyRange];

    BOOL isTesting = NSProcessInfo.processInfo.environment[@"XCTestConfigurationFilePath"] != nil;
    if (isTesting) {
        (void) self.terminalAccessibilityElement;
    } else {
        self.terminalAccessibilityElement = nil;
    }
}

- (void)layoutSubviews {
    [super layoutSubviews];
    if (self.terminal.loaded) {
        [self.terminal surfaceDidLayout];
        [self.terminal syncWindowSizeIfPossible];
    }
    if (self.terminalAccessibilityElement) {
        self.terminalAccessibilityElement.accessibilityFrameInContainerSpace = self.bounds;
        BOOL isTesting = NSProcessInfo.processInfo.environment[@"XCTestConfigurationFilePath"] != nil;
        if (isTesting) {
            if (self.window != nil) {
                CGRect frameInScreen = [self.window convertRect:[self convertRect:self.bounds toView:self.window] toCoordinateSpace:UIScreen.mainScreen.coordinateSpace];
                self.terminalAccessibilityElement.accessibilityFrame = frameInScreen;
            }
        }
    }
}

- (BOOL)canBecomeFirstResponder {
    return YES;
}

- (void)awakeFromNib {
    [super awakeFromNib];
    [self commonInit];
}

- (void)dealloc {
    self.terminal = nil;
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey,id> *)change context:(void *)context {
    if (object == _terminal) {
        if ([keyPath isEqualToString:@"loaded"]) {
            if (_terminal.loaded) {
                [self installTerminalView];
                [self _updateStyle];
                if (self.terminalFocused)
                    [self becomeFirstResponder];
            }
            NSString *terminalText = [_terminal screenTextForTesting];
            self.terminalAccessibilityElement.accessibilityValue = terminalText.length > 0 ? terminalText : @"No terminal output";
        } else if ([keyPath isEqualToString:@"contentChange"]) {
            // Update accessibilityValue when terminal content changes
            NSString *terminalText = [_terminal screenTextForTesting];
            self.terminalAccessibilityElement.accessibilityValue = terminalText.length > 0 ? terminalText : @"No terminal output";
            if ([self isRunningUITests]) {
                if (terminalText.length <= 256) {
                    NSUInteger previewLength = MIN((NSUInteger) 80, terminalText.length);
                    NSUInteger previewStart = terminalText.length - previewLength;
                    NSString *preview = terminalText.length > 0 ? [terminalText substringFromIndex:previewStart] : @"";
                    [ISHInstrumentation recordEvent:@"terminal.testing_accessibility.preview"
                                         attributes:@{ @"value_length": @(terminalText.length),
                                                       @"preview": preview ?: @"" }];
                }
                NSLog(@"[IXLandAccessibility] value_length=%lu",
                      (unsigned long) terminalText.length);
            }
        }
    }
}

- (void)setTerminal:(Terminal *)terminal {
    if (_terminal) {
        [_terminal removeObserver:self forKeyPath:@"loaded"];
        [_terminal removeObserver:self forKeyPath:@"contentChange"];
        [self uninstallTerminalView];
    }
    _terminal = terminal;

    if (_terminal) {
        [_terminal addObserver:self forKeyPath:@"contentChange" options:NSKeyValueObservingOptionInitial|NSKeyValueObservingOptionNew context:nil];
    }
    // Initialize or update accessibility proxy
    if (!self.terminalAccessibilityElement) {
        self.terminalAccessibilityElement = [[TerminalSurfaceAccessibilityElement alloc] initWithAccessibilityContainer:self];
        self.terminalAccessibilityElement.accessibilityIdentifier = @"TerminalSurface";
        self.terminalAccessibilityElement.accessibilityLabel = @"Terminal";
        self.terminalAccessibilityElement.accessibilityTraits = UIAccessibilityTraitAllowsDirectInteraction;
        self.terminalAccessibilityElement.accessibilityFrameInContainerSpace = self.bounds;
    }
    // Update value from terminal content
    NSString *terminalText = terminal ? [terminal screenTextForTesting] : nil;
    self.terminalAccessibilityElement.accessibilityValue = terminalText.length > 0 ? terminalText : @"No terminal output";

    // Handle nil terminal (shell-only mode) - skip all terminal setup
    if (_terminal == nil) {
        return;
    }

    [_terminal addObserver:self forKeyPath:@"loaded" options:NSKeyValueObservingOptionInitial context:nil];
    [self installTerminalView];
}

- (void)installTerminalView {
    // installTerminalView: no logging in production/test code
    UIView *superview = self.terminal.webView.superview;
    if (superview != nil) {
        NSAssert(superview == self, @"installing terminal that is already installed elsewhere");
        return;
    }

    UIView *terminalView = _terminal.webView;
    _terminal.enableVoiceOverAnnounce = YES;
    terminalView.frame = self.bounds;
    self.opaque = YES;
    UIColor *backgroundColor = [[UIColor alloc] ish_initWithHexString:UserPreferences.shared.palette.backgroundColor];
    self.backgroundColor = backgroundColor;
    self.scrollbarView.backgroundColor = backgroundColor;
    terminalView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;

    self.scrollbarView.contentView = nil;
    [self addSubview:terminalView];
    [self bringSubviewToFront:terminalView];
    if (self.inputBridgeField != nil) {
        [self bringSubviewToFront:self.inputBridgeField];
    }
}

- (void)uninstallTerminalView {
    // In shell-only mode, terminal may be nil - nothing to uninstall
    if (_terminal == nil)
        return;

    // remove old terminal
    UIView *superview = _terminal.webView.superview;
    if (superview != self) {
        NSAssert(superview == nil, @"uninstalling terminal that is installed elsewhere");
        return;
    }

    [_terminal.webView removeFromSuperview];
    self.scrollbarView.contentView = nil;
    _terminal.enableVoiceOverAnnounce = NO;
}

#pragma mark Styling

- (void)_updateStyle {
    NSAssert(NSThread.isMainThread, @"This method needs to be called on the main thread");
    // In shell-only mode, terminal is nil - skip style update
    if (self.terminal == nil || !self.terminal.loaded)
        return;
    UserPreferences *prefs = [UserPreferences shared];
    if (_overrideFontSize == prefs.fontSize.doubleValue) {
        _overrideFontSize = 0;
    }
    [self.terminal updateAppearanceStyle];
    [self.terminal updateFontSize:self.effectiveFontSize];
}

- (void)setOverrideFontSize:(CGFloat)overrideFontSize {
    _overrideFontSize = overrideFontSize;
    [self _updateStyle];
}

- (void)setOverrideAppearance:(enum OverrideAppearance)overrideAppearance {
    _overrideAppearance = overrideAppearance;
    [self _updateStyle];
}

- (CGFloat)effectiveFontSize {
    if (self.overrideFontSize != 0)
        return self.overrideFontSize;
    return UserPreferences.shared.fontSize.doubleValue;
}

#pragma mark Focus and scrolling

- (void)setTerminalFocused:(BOOL)terminalFocused {
    _terminalFocused = terminalFocused;
}

- (BOOL)becomeFirstResponder {
    self.terminalFocused = YES;
    BOOL focused = NO;
    if (self.usingInputBridge && self.inputBridgeField != nil) {
        self.inputBridgeField.userInteractionEnabled = YES;
        self.inputBridgeField.enabled = YES;
        [self bringSubviewToFront:self.inputBridgeField];
        focused = [self.inputBridgeField becomeFirstResponder];
    } else if (self.terminal != nil) {
        focused = [self.terminal requestFocus];
    }
    if (!focused) {
        focused = [super becomeFirstResponder];
    }
    _terminalFocused = focused;
    if (focused && self.inputBridgeField != nil)
        [self.inputBridgeField reloadInputViews];
    return focused;
}

- (BOOL)focusForTesting {
    if (!self.usingInputBridge || self.inputBridgeField == nil) {
        return [self becomeFirstResponder];
    }
    self.inputBridgeField.userInteractionEnabled = YES;
    self.inputBridgeField.enabled = YES;
    [self bringSubviewToFront:self.inputBridgeField];
    return [self.inputBridgeField becomeFirstResponder];
}

- (BOOL)isFirstResponder {
    BOOL bridgeFirstResponder = self.inputBridgeField != nil && self.inputBridgeField.isFirstResponder;
    BOOL terminalFirstResponder = self.terminal.webView != nil && self.terminal.webView.isFirstResponder;
    return [super isFirstResponder] || bridgeFirstResponder || terminalFirstResponder;
}

- (UIInputView *)inputAccessoryView {
    return _inputAccessoryView;
}

- (void)setInputAccessoryView:(UIInputView *)inputAccessoryView {
    _inputAccessoryView = inputAccessoryView;
    self.inputBridgeField.inputAccessoryView = inputAccessoryView;
    if (self.inputBridgeField != nil && self.inputBridgeField.isFirstResponder) {
        [self.inputBridgeField reloadInputViews];
    }
}

- (BOOL)textField:(UITextField *)textField shouldChangeCharactersInRange:(NSRange)range replacementString:(NSString *)string {
    if (string.length > 0) {
        [self insertText:string];
    }
    textField.text = @"";
    return NO;
}

- (BOOL)textFieldShouldReturn:(UITextField *)textField {
    [self insertText:@"\n"];
    textField.text = @"";
    return NO;
}
- (BOOL)resignFirstResponder {
    self.terminalFocused = NO;
    if (self.inputBridgeField != nil && self.inputBridgeField.isFirstResponder) {
        return [self.inputBridgeField resignFirstResponder];
    }
    if (self.terminal.webView != nil && self.terminal.webView.isFirstResponder) {
        return [self.terminal.webView resignFirstResponder];
    }
    return [super resignFirstResponder];
}
- (void)windowDidBecomeKey:(NSNotification *)notif {
    self.terminalFocused = YES;
}
- (void)windowDidResignKey:(NSNotification *)notif {
    self.terminalFocused = NO;
}

- (IBAction)loseFocus:(id)sender {
    [self resignFirstResponder];
}

- (void)willMoveToWindow:(UIWindow *)newWindow {
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    if (self.window != nil) {
        [center removeObserver:self
                          name:UIWindowDidBecomeKeyNotification
                        object:self.window];
        [center removeObserver:self
                          name:UIWindowDidResignKeyNotification
                        object:self.window];
    }
    if (newWindow != nil) {
        [center addObserver:self
                   selector:@selector(windowDidBecomeKey:)
                       name:UIWindowDidBecomeKeyNotification
                     object:newWindow];
        [center addObserver:self
                   selector:@selector(windowDidResignKey:)
                       name:UIWindowDidResignKeyNotification
                     object:newWindow];
    }

    // If running under XCTest and we are moving into a window, notify that
    // the accessibility proxy can be inserted at the window level earlier
    // than viewDidAppear. The TerminalViewController will call
    // ensureWindowAccessibilityElement when it receives this notification.
    BOOL isTesting = NSProcessInfo.processInfo.environment[@"XCTestConfigurationFilePath"] != nil;
    if (isTesting && newWindow != nil) {
        [ISHInstrumentation recordEvent:@"terminal.accessibility.proxy.didMoveToWindow" attributes:@{ @"has_window": @(newWindow != nil) }];
        [[NSNotificationCenter defaultCenter] postNotificationName:@"IXLand.TerminalViewDidMoveToWindowNotification" object:self userInfo:@{ @"window": newWindow }];
        dispatch_async(dispatch_get_main_queue(), ^{
            [self becomeFirstResponder];
        });
    }
}

- (void)scrollViewDidScroll:(UIScrollView *)scrollView {
    // Ghostty renders its own surface and manages scrolling internally.
    // Keep this method to satisfy UIScrollViewDelegate wiring.
    (void) scrollView;
}

- (void)setKeyboardAppearance:(UIKeyboardAppearance)keyboardAppearance {
    BOOL needsFirstResponderDance = self.isFirstResponder && _keyboardAppearance != keyboardAppearance;
    if (needsFirstResponderDance) {
        [self resignFirstResponder];
    }
    _keyboardAppearance = keyboardAppearance;
    if (needsFirstResponderDance) {
        [self becomeFirstResponder];
    }
    if (keyboardAppearance == UIKeyboardAppearanceLight) {
        self.scrollbarView.indicatorStyle = UIScrollViewIndicatorStyleBlack;
    } else {
        self.scrollbarView.indicatorStyle = UIScrollViewIndicatorStyleWhite;
    }
}

#pragma mark Keyboard Input

// implementing these makes a keyboard pop up when this view is first responder

- (void)insertText:(NSString *)text {
    // In shell-only mode, terminal is nil - no input to send
    if (self.terminal == nil)
        return;

    // PROOF: record that UI inserted text into TerminalView. This is a
    // minimal, non-invasive instrumentation point to correlate typed input
    // from XCTest with later terminal/sendInput events.
    NSDictionary *uiAttrs = @{
        @"char_count": @(text.length),
        @"first_char": @(text.length > 0 ? (unsigned int)[text characterAtIndex:0] : 0)
    };
    [ISHInstrumentation recordEvent:@"terminal.ui.insert_text" attributes:uiAttrs];

    self.markedText = nil;

    if (self.controlKey.highlighted)
        self.controlKey.selected = YES;
    if (self.controlKey.selected) {
        if (!self.controlKey.highlighted)
            self.controlKey.selected = NO;
        if (text.length == 1)
            return [self insertControlChar:[text characterAtIndex:0]];
    }

    text = [text stringByReplacingOccurrencesOfString:@"\n" withString:@"\r"];
    NSData *data = [text dataUsingEncoding:NSUTF8StringEncoding];
    [self.terminal sendInput:data];
}

- (void)insertControlChar:(char)ch {
    // In shell-only mode, terminal is nil - no input to send
    if (self.terminal == nil)
        return;

    if (strchr(controlKeys, ch) != NULL) {
        if (ch == ' ') ch = '\0';
        if (ch == '2') ch = '@';
        if (ch == '6') ch = '^';
        if (ch != '\0')
            ch = toupper(ch) ^ 0x40;
        [self.terminal sendInput:[NSData dataWithBytes:&ch length:1]];
    }
}

- (void)deleteBackward {
    [self insertText:@"\x7f"];
}

- (BOOL)hasText {
    return YES; // it's always ok to send a "delete"
}

#pragma mark IME Input and Selection

- (BOOL)shouldCommitMarkedTextImmediately:(NSString *)markedText
                            selectedRange:(NSRange)selectedRange {
    if (markedText.length == 0)
        return NO;
    if (selectedRange.length != 0 || selectedRange.location != markedText.length)
        return NO;

    __block BOOL shouldCommit = YES;
    [markedText enumerateSubstringsInRange:NSMakeRange(0, markedText.length)
                                   options:NSStringEnumerationByComposedCharacterSequences
                                usingBlock:^(NSString * _Nullable substring, NSRange substringRange,
                                             NSRange enclosingRange, BOOL * _Nonnull stop) {
        (void) substringRange;
        (void) enclosingRange;
        if (substring.length == 0)
            return;

        unichar first = [substring characterAtIndex:0];
        BOOL isASCIIControl = first == '\n' || first == '\r' || first == '\t';
        BOOL isASCIIPrintable = first >= 0x20 && first <= 0x7e;
        if (substring.length != 1 || (!isASCIIPrintable && !isASCIIControl)) {
            shouldCommit = NO;
            *stop = YES;
        }
    }];
    return shouldCommit;
}

- (void)setMarkedText:(nullable NSString *)markedText selectedRange:(NSRange)selectedRange {
    if ([self shouldCommitMarkedTextImmediately:markedText selectedRange:selectedRange]) {
        self.suppressNextMarkedTextCommit = YES;
        self.markedText = nil;
        [self insertText:markedText];
        return;
    }
    self.markedText = markedText;
}

- (void)unmarkText {
    if (self.suppressNextMarkedTextCommit) {
        self.suppressNextMarkedTextCommit = NO;
        self.markedText = nil;
        return;
    }
    [self insertText:self.markedText];
    self.markedText = nil;
}

- (UITextRange *)markedTextRange {
    if (self.markedText != nil)
        return self.markedRange;
    return nil;
}

// The only reason to have this selected range is to prevent the "speak selection" context action from failing to get the current selection and falling back on calling copy:. It doesn't even have to work, it seems...

- (UITextRange *)selectedTextRange {
    return self.selectedRange;
}

- (NSString *)textInRange:(UITextRange *)range {
    if (range == self.markedRange)
        return self.markedText;
    if (range == self.selectedRange)
        return @"";
    return nil;
}

- (id)insertDictationResultPlaceholder {
    return @"";
}
- (void)removeDictationResultPlaceholder:(id)placeholder willInsertResult:(BOOL)willInsertResult {
}

#pragma mark Keyboard Actions

- (void)paste:(id)sender {
    NSString *string = UIPasteboard.generalPasteboard.string;
    if (string) {
        [self insertText:string];
    }
}

- (void)copy:(id)sender {
    // In shell-only mode or before terminal ready, skip copy.
    if (self.terminal == nil || !self.terminal.loaded)
        return;
    (void) sender;
}

- (void)clearScrollback:(UIKeyCommand *)command {
    // In shell-only mode or before terminal ready, skip clear scrollback.
    if (self.terminal == nil || !self.terminal.loaded)
        return;
    (void) command;
}

#pragma mark Floating cursor

- (void)updateFloatingCursorSensitivity {
    // In shell-only mode, or before terminal ready, skip floating cursor sensitivity update.
    if (self.terminal == nil || !self.terminal.loaded)
        return;
    self.floatingCursorSensitivity = CGSizeMake(8.0, 16.0);
}

- (struct rowcol)rowcolFromPoint:(CGPoint)point {
    CGSize sensitivity = self.actualFloatingCursorSensitivity;
    return (struct rowcol) {
        .row = (int) (-point.y / sensitivity.height),
        .col = (int) (point.x / sensitivity.width),
    };
}

- (void)beginFloatingCursorAtPoint:(CGPoint)point {
    self.actualFloatingCursorSensitivity = self.floatingCursorSensitivity;
    self.floatingCursor = [self rowcolFromPoint:point];
}

- (void)updateFloatingCursorAtPoint:(CGPoint)point {
    // In shell-only mode, terminal is nil - skip floating cursor update
    if (self.terminal == nil)
        return;
    struct rowcol newPos = [self rowcolFromPoint:point];
    int rowDiff = newPos.row - self.floatingCursor.row;
    int colDiff = newPos.col - self.floatingCursor.col;
    NSMutableString *arrows = [NSMutableString string];
    for (int i = 0; i < abs(rowDiff); i++) {
        [arrows appendString:[self.terminal arrow:rowDiff > 0 ? 'A': 'B']];
    }
    for (int i = 0; i < abs(colDiff); i++) {
        [arrows appendString:[self.terminal arrow:colDiff > 0 ? 'C': 'D']];
    }
    [self insertText:arrows];
    self.floatingCursor = newPos;
}

- (void)endFloatingCursor {
    self.floatingCursor = (struct rowcol) {};
}

#pragma mark Keyboard Traits

- (UITextSmartDashesType)smartDashesType API_AVAILABLE(ios(11)) {
    return UITextSmartDashesTypeNo;
}
- (UITextSmartQuotesType)smartQuotesType API_AVAILABLE(ios(11)) {
    return UITextSmartQuotesTypeNo;
}
- (UITextSmartInsertDeleteType)smartInsertDeleteType API_AVAILABLE(ios(11)) {
    return UITextSmartInsertDeleteTypeNo;
}
- (UITextAutocapitalizationType)autocapitalizationType {
    return UITextAutocapitalizationTypeNone;
}
- (UITextAutocorrectionType)autocorrectionType {
    return UITextAutocorrectionTypeNo;
}
// Apparently required on iOS 15+: https://stackoverflow.com/a/72359764
- (UITextSpellCheckingType)spellCheckingType {
    return UITextSpellCheckingTypeNo;
}

#pragma mark Hardware Keyboard

- (void)handleKeyCommand:(UIKeyCommand *)command {
    // In shell-only mode, terminal is nil - no key commands to process
    if (self.terminal == nil)
        return;

    NSString *key = command.input;
    if (command.modifierFlags == 0) {
        if ([key isEqualToString:@"`"] && UserPreferences.shared.backtickMapEscape)
            key = UIKeyInputEscape;
        if ([key isEqualToString:UIKeyInputEscape])
            key = @"\x1b";
        else if ([key isEqualToString:UIKeyInputUpArrow])
            key = [self.terminal arrow:'A'];
        else if ([key isEqualToString:UIKeyInputDownArrow])
            key = [self.terminal arrow:'B'];
        else if ([key isEqualToString:UIKeyInputLeftArrow])
            key = [self.terminal arrow:'D'];
        else if ([key isEqualToString:UIKeyInputRightArrow])
            key = [self.terminal arrow:'C'];
        [self insertText:key];
    } else if (command.modifierFlags & UIKeyModifierShift) {
        [self insertText:[key uppercaseString]];
    } else if (command.modifierFlags & UIKeyModifierAlternate) {
        [self insertText:[@"\x1b" stringByAppendingString:key]];
    } else if (command.modifierFlags & UIKeyModifierAlphaShift) {
        [self handleCapsLockWithCommand:command];
    } else if (command.modifierFlags & UIKeyModifierControl || command.modifierFlags & UIKeyModifierAlphaShift) {
        if (key.length == 0)
            return;
        if ([key isEqualToString:@"2"])
            key = @"@";
        else if ([key isEqualToString:@"6"])
            key = @"^";
        else if ([key isEqualToString:@"-"])
            key = @"_";
        [self insertControlChar:[key characterAtIndex:0]];
    }
}

static const char *alphabet = "abcdefghijklmnopqrstuvwxyz";
static const char *controlKeys = "abcdefghijklmnopqrstuvwxyz@^26-=[]\\ ";
static const char *metaKeys = "abcdefghijklmnopqrstuvwxyz0123456789-=[]\\;',./";

- (NSArray<UIKeyCommand *> *)keyCommands {
    if (_keyCommands != nil)
        return _keyCommands;
    _keyCommands = [NSMutableArray new];
    [self addKeys:controlKeys withModifiers:UIKeyModifierControl];
    for (NSString *specialKey in @[UIKeyInputEscape, UIKeyInputUpArrow, UIKeyInputDownArrow,
                                   UIKeyInputLeftArrow, UIKeyInputRightArrow, @"\t"]) {
        [self addKey:specialKey withModifiers:0];
    }
    if (UserPreferences.shared.capsLockMapping != CapsLockMapNone) {
        if (@available(iOS 13, *)); else {
            [self addKeys:controlKeys withModifiers:UIKeyModifierAlphaShift];
            [self addKeys:alphabet withModifiers:0];
            [self addKeys:alphabet withModifiers:UIKeyModifierShift];
            [self addKey:@"" withModifiers:UIKeyModifierAlphaShift]; // otherwise tap of caps lock can switch layouts
        }
    }
    if (UserPreferences.shared.optionMapping == OptionMapEsc) {
        [self addKeys:metaKeys withModifiers:UIKeyModifierAlternate];
    }
    if (UserPreferences.shared.backtickMapEscape) {
        [self addKey:@"`" withModifiers:0];
    }
    UIKeyCommand *clearScrollbackCommand = [UIKeyCommand keyCommandWithInput:@"k"
                                                               modifierFlags:UIKeyModifierCommand|UIKeyModifierShift
                                                                      action:@selector(clearScrollback:)];
    clearScrollbackCommand.title = @"Clear Scrollback";
    [_keyCommands addObject:clearScrollbackCommand];
    return _keyCommands;
}

- (void)addKeys:(const char *)keys withModifiers:(UIKeyModifierFlags)modifiers {
    for (size_t i = 0; keys[i] != '\0'; i++) {
        [self addKey:[NSString stringWithFormat:@"%c", keys[i]] withModifiers:modifiers];
    }
}

- (void)addKey:(NSString *)key withModifiers:(UIKeyModifierFlags)modifiers {
    UIKeyCommand *command = [UIKeyCommand keyCommandWithInput:key
                                                modifierFlags:modifiers
                                                       action:@selector(handleKeyCommand:)];
    if (@available(iOS 15, *)) {
        command.wantsPriorityOverSystemBehavior = YES;
    }
    [_keyCommands addObject:command];
}

- (void)keyCommandTriggered:(UIKeyCommand *)sender {
    dispatch_async(dispatch_get_main_queue(), ^{
        [self handleKeyCommand:sender];
    });
}

- (void)handleCapsLockWithCommand:(UIKeyCommand *)command {
    CapsLockMapping target = UserPreferences.shared.capsLockMapping;
    NSString *newInput = command.input ? command.input : @"";
    UIKeyModifierFlags flags = command.modifierFlags;
    flags ^= UIKeyModifierAlphaShift;
    if(target == CapsLockMapEscape) {
        newInput = UIKeyInputEscape;
    } else if(target == CapsLockMapControl) {
        if([newInput length] == 0) {
            return;
        }
        flags |= UIKeyModifierControl;
    } else {
        return;
    }

    UIKeyCommand *newCommand = [UIKeyCommand keyCommandWithInput:newInput
                                                   modifierFlags:flags
                                                          action:@selector(keyCommandTriggered:)];
    [self handleKeyCommand:newCommand];
}

- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    if (@available(iOS 13.4, *)) {
        NSMutableSet<UIPress *> *unhandledPresses = [presses mutableCopy];
        for (UIPress *press in presses) {
            UIKey *key = press.key;
            if (key == nil)
                continue;

            BOOL handled = NO;
            UIKeyModifierFlags modifiers = key.modifierFlags;
            if (UserPreferences.shared.overrideControlSpace &&
                key.keyCode == UIKeyboardHIDUsageKeyboardSpacebar &&
                modifiers & UIKeyModifierControl) {
                [self insertControlChar:' '];
                handled = YES;
            } else if (modifiers & UIKeyModifierCommand) {
                handled = NO;
            } else if (modifiers & UIKeyModifierControl) {
                NSString *characters = key.charactersIgnoringModifiers;
                if (characters.length == 1) {
                    [self insertControlChar:(char) [characters characterAtIndex:0]];
                    handled = YES;
                }
            } else {
                switch (key.keyCode) {
                    case UIKeyboardHIDUsageKeyboardReturnOrEnter:
                    case UIKeyboardHIDUsageKeypadEnter:
                        [self insertText:@"\n"];
                        handled = YES;
                        break;
                    case UIKeyboardHIDUsageKeyboardDeleteOrBackspace:
                        [self deleteBackward];
                        handled = YES;
                        break;
                    case UIKeyboardHIDUsageKeyboardTab:
                        [self insertText:@"\t"];
                        handled = YES;
                        break;
                    case UIKeyboardHIDUsageKeyboardEscape:
                        [self insertText:@"\x1b"];
                        handled = YES;
                        break;
                    case UIKeyboardHIDUsageKeyboardUpArrow:
                        [self insertText:[self.terminal arrow:'A']];
                        handled = YES;
                        break;
                    case UIKeyboardHIDUsageKeyboardDownArrow:
                        [self insertText:[self.terminal arrow:'B']];
                        handled = YES;
                        break;
                    case UIKeyboardHIDUsageKeyboardLeftArrow:
                        [self insertText:[self.terminal arrow:'D']];
                        handled = YES;
                        break;
                    case UIKeyboardHIDUsageKeyboardRightArrow:
                        [self insertText:[self.terminal arrow:'C']];
                        handled = YES;
                        break;
                    default: {
                        NSString *characters = key.characters;
                        if (characters.length > 0) {
                            if (modifiers & UIKeyModifierAlternate) {
                                characters = [@"\x1b" stringByAppendingString:characters];
                            }
                            [self insertText:characters];
                            handled = YES;
                        }
                        break;
                    }
                }
            }

            if (handled) {
                [unhandledPresses removeObject:press];
            }
        }
        if (unhandledPresses.count == 0) {
            return;
        }
        presses = unhandledPresses;
    }
    return [super pressesBegan:presses withEvent:event];
}

#pragma mark UITextInput stubs

#if 0
#define LogStub() NSLog(@"%s", __func__)
#else
#define LogStub()
#endif

- (NSWritingDirection)baseWritingDirectionForPosition:(nonnull UITextPosition *)position inDirection:(UITextStorageDirection)direction { LogStub(); return NSWritingDirectionLeftToRight; }
- (void)setBaseWritingDirection:(NSWritingDirection)writingDirection forRange:(nonnull UITextRange *)range { LogStub(); }
- (UITextPosition *)beginningOfDocument { LogStub(); return [self terminalDocumentPosition]; }
- (CGRect)caretRectForPosition:(nonnull UITextPosition *)position { LogStub(); return CGRectZero; }
- (nullable UITextRange *)characterRangeAtPoint:(CGPoint)point { LogStub(); return [self terminalEmptyRange]; }
- (nullable UITextRange *)characterRangeByExtendingPosition:(nonnull UITextPosition *)position inDirection:(UITextLayoutDirection)direction { LogStub(); return [self terminalEmptyRange]; }
- (nullable UITextPosition *)closestPositionToPoint:(CGPoint)point { LogStub(); return [self terminalDocumentPosition]; }
- (nullable UITextPosition *)closestPositionToPoint:(CGPoint)point withinRange:(nonnull UITextRange *)range { LogStub(); return [self terminalDocumentPosition]; }
- (NSComparisonResult)comparePosition:(nonnull UITextPosition *)position toPosition:(nonnull UITextPosition *)other {
    LogStub();
    NSInteger lhs = [self terminalPositionFromUITextPosition:position].offset;
    NSInteger rhs = [self terminalPositionFromUITextPosition:other].offset;
    if (lhs < rhs)
        return NSOrderedAscending;
    if (lhs > rhs)
        return NSOrderedDescending;
    return NSOrderedSame;
}
- (UITextPosition *)endOfDocument { LogStub(); return [self terminalDocumentPosition]; }
- (CGRect)firstRectForRange:(nonnull UITextRange *)range { LogStub(); return CGRectZero; }
- (NSDictionary<NSAttributedStringKey,id> *)markedTextStyle { LogStub(); return nil; }
- (void)setMarkedTextStyle:(NSDictionary<NSAttributedStringKey,id> *)markedTextStyle { LogStub(); }
- (NSInteger)offsetFromPosition:(nonnull UITextPosition *)from toPosition:(nonnull UITextPosition *)toPosition {
    LogStub();
    return [self terminalPositionFromUITextPosition:toPosition].offset -
           [self terminalPositionFromUITextPosition:from].offset;
}
- (nullable UITextPosition *)positionFromPosition:(nonnull UITextPosition *)position inDirection:(UITextLayoutDirection)direction offset:(NSInteger)offset {
    LogStub();
    return [self positionFromPosition:position offset:offset];
}
- (nullable UITextPosition *)positionFromPosition:(nonnull UITextPosition *)position offset:(NSInteger)offset {
    LogStub();
    NSInteger base = [self terminalPositionFromUITextPosition:position].offset;
    NSInteger next = MAX(0, base + offset);
    return [TerminalTextPosition positionWithOffset:next];
}
- (nullable UITextPosition *)positionWithinRange:(nonnull UITextRange *)range farthestInDirection:(UITextLayoutDirection)direction {
    LogStub();
    TerminalTextRange *textRange = [self terminalRangeFromUITextRange:range];
    switch (direction) {
        case UITextLayoutDirectionLeft:
        case UITextLayoutDirectionUp:
            return textRange.start;
        case UITextLayoutDirectionRight:
        case UITextLayoutDirectionDown:
            return textRange.end;
    }
}
- (void)replaceRange:(nonnull UITextRange *)range withText:(nonnull NSString *)text { LogStub(); }
- (void)setSelectedTextRange:(UITextRange *)selectedTextRange {
    LogStub();
    self.selectedRange = selectedTextRange ?: [self terminalEmptyRange];
}
- (nonnull NSArray<UITextSelectionRect *> *)selectionRectsForRange:(nonnull UITextRange *)range { LogStub(); return @[]; }
- (nullable UITextRange *)textRangeFromPosition:(nonnull UITextPosition *)fromPosition toPosition:(nonnull UITextPosition *)toPosition {
    LogStub();
    NSInteger from = [self terminalPositionFromUITextPosition:fromPosition].offset;
    NSInteger to = [self terminalPositionFromUITextPosition:toPosition].offset;
    if (to < from) {
        NSInteger tmp = from;
        from = to;
        to = tmp;
    }
    return [TerminalTextRange rangeWithStart:from end:to];
}

- (BOOL)isAccessibilityElement {
    return NO;
}

- (BOOL)accessibilityActivate {
    return [self becomeFirstResponder];
}

- (NSString *)accessibilityValue {
    NSString *terminalText = [self.terminal screenTextForTesting];
    return terminalText.length > 0 ? terminalText : @"No terminal output";
}

- (NSArray *)accessibilityElements {
    if ([self isRunningUITests]) {
        return self.inputBridgeField != nil ? @[self.terminalAccessibilityElement, self.inputBridgeField] : @[self.terminalAccessibilityElement];
    }
    // Always expose only the TerminalView-owned accessibility proxy. Do not
    // also expose the WKWebView or create window-level synthetic elements.
    if (self.terminalAccessibilityElement) {
        return @[self.terminalAccessibilityElement];
    }
    return nil;
}

@end

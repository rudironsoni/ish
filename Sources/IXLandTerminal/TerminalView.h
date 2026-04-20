//
//  TerminalView.h
//  iSH
//
//  Created by Theodore Dubois on 11/3/17.
//

@import UIKit;
#import "Terminal.h"

enum OverrideAppearance {
    OverrideAppearanceNone,
    OverrideAppearanceLight,
    OverrideAppearanceDark,
};

@interface TerminalView : UIView <UITextInput, WKScriptMessageHandler, UIScrollViewDelegate>

@property (nonatomic) CGFloat overrideFontSize;
@property (readonly) CGFloat effectiveFontSize;
@property (nonatomic) enum OverrideAppearance overrideAppearance;
@property (nonatomic, strong) UIAccessibilityElement *terminalAccessibilityElement;

@property (nonatomic) UIKeyboardAppearance keyboardAppearance;

@property (weak, nonatomic) IBOutlet UIInputView *inputAccessoryView;
@property (weak, nonatomic) IBOutlet UIButton *controlKey;

@property (nonatomic) Terminal *terminal;

// UITextInput required methods
- (UITextPosition *)beginningOfDocument;
- (UITextPosition *)endOfDocument;
- (nullable UITextRange *)characterRangeAtPoint:(CGPoint)point;
- (nullable UITextRange *)characterRangeByExtendingPosition:(UITextPosition *)position inDirection:(UITextLayoutDirection)direction;
- (nullable UITextPosition *)closestPositionToPoint:(CGPoint)point;
- (nullable UITextPosition *)closestPositionToPoint:(CGPoint)point withinRange:(UITextRange *)range;
- (NSComparisonResult)comparePosition:(UITextPosition *)position toPosition:(UITextPosition *)other;
- (UITextWritingDirection)baseWritingDirectionForPosition:(UITextPosition *)position inDirection:(UITextStorageDirection)direction;
- (void)setBaseWritingDirection:(UITextWritingDirection)writingDirection forRange:(UITextRange *)range;
- (CGRect)caretRectForPosition:(UITextPosition *)position;
- (CGRect)firstRectForRange:(UITextRange *)range;
- (void)setMarkedText:(nullable NSString *)markedText selectedRange:(NSRange)selectedRange;
- (void)unmarkText;
- (nullable UITextRange *)markedTextRange;
- (void)setMarkedTextStyle:(nullable NSDictionary<NSAttributedStringKey,id> *)markedTextStyle;
- (nullable NSDictionary<NSAttributedStringKey,id> *)markedTextStyle;
- (UITextRange *)selectedTextRange;
- (void)setSelectedTextRange:(UITextRange *)selectedTextRange;
- (NSString *)textInRange:(UITextRange *)range;
- (void)replaceRange:(UITextRange *)range withText:(NSString *)text;
- (NSInteger)offsetFromPosition:(UITextPosition *)from toPosition:(UITextPosition *)toPosition;
- (nullable UITextPosition *)positionFromPosition:(UITextPosition *)position inDirection:(UITextLayoutDirection)direction offset:(NSInteger)offset;
- (nullable UITextPosition *)positionFromPosition:(UITextPosition *)position offset:(NSInteger)offset;
- (nullable UITextPosition *)positionWithinRange:(UITextRange *)range farthestInDirection:(UITextLayoutDirection)direction;
- (nullable UITextRange *)textRangeFromPosition:(UITextPosition *)fromPosition toPosition:(UITextPosition *)toPosition;
- (BOOL)hasText;
- (void)insertText:(NSString *)text;
- (void)deleteBackward;

@end

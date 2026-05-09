//
//  Terminal.h
//  iSH
//
//  Created by Theodore Dubois on 10/18/17.
//

#import <UIKit/UIKit.h>
#include <stdbool.h>

struct tty;
struct linux_tty;

@interface Terminal : NSObject

+ (Terminal *)terminalWithType:(int)type number:(int)number;
// Returns a strong struct tty and a Terminal that has a weak reference to the same tty
+ (Terminal *)createPseudoTerminal:(struct tty **)tty;

+ (Terminal *)terminalWithUUID:(NSUUID *)uuid;
@property (readonly) NSUUID *uuid;

+ (void)convertCommand:(NSArray<NSString *> *)command toArgs:(char *)argv limitSize:(size_t)maxSize;

- (int)sendOutput:(const void *)buf length:(int)len;
- (void)sendInput:(NSData *)input;
- (BOOL)becomeInputResponder;
- (BOOL)requestFocus;
- (BOOL)focusEditableSurface;
- (void)updateFontSize:(CGFloat)fontSize;
- (void)attachTTY:(struct tty *)tty;
- (void)attachLinuxTTY:(struct linux_tty *)tty;
- (int)roomForOutput;

- (NSString *)arrow:(char)direction;

// Make this terminal no longer be the singleton terminal with its type and number. Will happen eventually if all references go away, but sometimes you want it to happen now.
- (void)destroy;

// Test-only: Get the raw TTY buffer content for smoke tests
// Returns the current buffer content from the TTY layer without modifying state
- (NSString *)screenTextForTesting;

@property (readonly) UIView *webView;
@property (nonatomic) BOOL enableVoiceOverAnnounce;
@property (nonatomic) uint64_t attemptSequence;
@property (nonatomic) uint64_t sessionGeneration;
@property (nonatomic) int64_t guestPID;
@property (nonatomic) BOOL restartPath;
@property (nonatomic) BOOL hasSessionTerminal;
@property (nonatomic) BOOL firstPTYByteSeen;
// Use KVO on this
@property (readonly) BOOL loaded;

@end

#ifndef IXLAND_NSOBJ_T_DEFINED
#define IXLAND_NSOBJ_T_DEFINED
typedef const void *nsobj_t;
#endif
bool Terminal_bindGuestTTY(struct tty *tty, nsobj_t *terminal_out);

extern struct tty_driver terminal_console_driver;
extern struct tty_driver terminal_pty_driver;

//
//  Terminal.m
//  iSH
//
//  Created by Theodore Dubois on 10/18/17.
//

#import "Terminal.h"
#import "DelayedUITask.h"
#import "UserPreferences.h"
#import <ISHInstrumentation.h>
#import "LinuxInterop.h"
#import <IXLandLinuxRuntime/fs/devices.h>
#import <IXLandLinuxRuntime/fs/tty.h>
#import <IXLandLinuxRuntime/fs/devices.h>

extern struct tty_driver ios_pty_driver;

typedef struct tty *tty_t;

@interface Terminal () <WKScriptMessageHandler> {
    lock_t _dataLock;
    cond_t _dataConsumed;
}

@property BOOL loaded;
@property (nonatomic) tty_t tty;
// lock with dataLock for !linux and @synchronized(self) for linux
@property (nonatomic) NSMutableData *pendingData;
// sending output is an asynchronous thing due to javascript, this is used to ensure it doesn't happen twice at once
@property (nonatomic) BOOL outputInProgress;

@property DelayedUITask *refreshTask;
@property DelayedUITask *scrollToBottomTask;

@property BOOL applicationCursor;
@property (nonatomic) NSUInteger contentChange;
@property (nonatomic) NSMutableString *testingTranscript;

@property NSNumber *terminalsKey;
@property NSUUID *uuid;
@property (nonatomic) struct linux_tty *linuxTTY;

@end

@interface CustomWebView : WKWebView
@end
@implementation CustomWebView
- (BOOL)canBecomeFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    if (self.window == nil || !self.window.isKeyWindow) {
        return NO;
    }
    return [super becomeFirstResponder];
}

- (BOOL)canPerformAction:(SEL)action withSender:(id)sender {
    if (action == @selector(copy:) || action == @selector(paste:)) {
        return NO;
    }
    return [super canPerformAction:action withSender:sender];
}
@end

@implementation Terminal
@synthesize webView = _webView;

static const int BUF_SIZE = 1<<14;

static NSMapTable<NSNumber *, Terminal *> *terminals;
static NSMapTable<NSUUID *, Terminal *> *terminalsByUUID;

- (instancetype)initWithType:(int)type number:(int)num {
    @synchronized (Terminal.class) {
        self.terminalsKey = @(dev_make(type, num));
        Terminal *terminal = [terminals objectForKey:self.terminalsKey];
        if (terminal)
            return terminal;

        if (self = [super init]) {
            self.pendingData = [[NSMutableData alloc] initWithCapacity:BUF_SIZE];
            self.contentChange = 0;
            self.testingTranscript = [NSMutableString string];
            self.refreshTask = [[DelayedUITask alloc] initWithTarget:self action:@selector(refresh)];
            self.scrollToBottomTask = [[DelayedUITask alloc] initWithTarget:self action:@selector(scrollToBottom)];
            lock_init(&_dataLock);
            cond_init(&_dataConsumed);

            [terminals setObject:self forKey:self.terminalsKey];
            self.uuid = [NSUUID UUID];
            [terminalsByUUID setObject:self forKey:self.uuid];
        }
        return self;
    }
}

- (WKWebView *)webView {
    if (_webView == nil) {
        WKWebViewConfiguration *config = [WKWebViewConfiguration new];
        [config.userContentController addScriptMessageHandler:self name:@"load"];
        [config.userContentController addScriptMessageHandler:self name:@"log"];
        [config.userContentController addScriptMessageHandler:self name:@"sendInput"];
        [config.userContentController addScriptMessageHandler:self name:@"resize"];
        [config.userContentController addScriptMessageHandler:self name:@"propUpdate"];
        // Make the web view really big so that if a program tries to write to the terminal before it's displayed, the text probably won't wrap too badly.
        CGRect webviewSize = CGRectMake(0, 0, 10000, 10000);
        _webView = [[CustomWebView alloc] initWithFrame:webviewSize configuration:config];
        if (@available(macOS 13.3, iOS 16.4, tvOS 16.4, *))
            _webView.inspectable = YES;
        _webView.scrollView.scrollEnabled = NO;
        // Do not mark the WKWebView itself as a top-level accessibility
        // element. TerminalView is responsible for exposing a single honest
        // TerminalSurface accessibility proxy owned by the TerminalView.
        NSURL *xtermHtmlFile = [NSBundle.mainBundle URLForResource:@"term" withExtension:@"html"];
        [_webView loadFileURL:xtermHtmlFile allowingReadAccessToURL:xtermHtmlFile];
    }
    return _webView;
}

+ (Terminal *)createPseudoTerminal:(struct tty **)tty {
    *tty = pty_open_fake(&ios_pty_driver);
    if (IS_ERR(*tty))
        return nil;
    return (__bridge Terminal *) (*tty)->data;
}

- (void)setTty:(tty_t)tty {
    @synchronized (self) {
        _tty = tty;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        [self syncWindowSize];
    });
}

- (void)userContentController:(WKUserContentController *)userContentController didReceiveScriptMessage:(WKScriptMessage *)message {
    if ([message.name isEqualToString:@"load"]) {
        self.loaded = YES;
        [self.refreshTask schedule];
        // make sure this setting works if it's set before loading
        self.enableVoiceOverAnnounce = self.enableVoiceOverAnnounce;
    } else if ([message.name isEqualToString:@"log"]) {
        // Log messages from terminal JS -> native are intentionally silent in
        // product code.
    } else if ([message.name isEqualToString:@"sendInput"]) {
        NSData *data = [message.body dataUsingEncoding:NSUTF8StringEncoding];
        [self sendInput:data];
    } else if ([message.name isEqualToString:@"resize"]) {
        [self syncWindowSize];
    } else if ([message.name isEqualToString:@"propUpdate"]) {
        [self setValue:message.body[1] forKey:message.body[0]];
    }
}

- (void)syncWindowSize {
    [self.webView evaluateJavaScript:@"exports.getSize()" completionHandler:^(NSArray<NSNumber *> *dimensions, NSError *error) {
        int cols = dimensions[0].intValue;
        int rows = dimensions[1].intValue;
        struct linux_tty *linuxTTY = nil;
        @synchronized (self) {
            linuxTTY = self.linuxTTY;
        }
        if (linuxTTY != NULL) {
            linuxTTY->ops->resize(linuxTTY, cols, rows);
            return;
        }
        if (self.tty == NULL)
            return;
        lock(&self.tty->lock);
        tty_set_winsize(self.tty, (struct winsize_) {.col = cols, .row = rows});
        unlock(&self.tty->lock);
    }];
}

- (BOOL)becomeInputResponder {
    return [self focusEditableSurface];
}

- (BOOL)requestFocus {
    return [self focusEditableSurface];
}

- (BOOL)focusEditableSurface {
    if (!self.webView || !self.webView.window) {
        return NO;
    }
    NSString *script = @"term.focus();";
    [self.webView evaluateJavaScript:script completionHandler:^(id result, NSError *error) {
        if (error) {
            NSLog(@"JS focus error: %@", error);
        }
    }];
    return YES;
}

- (void)setEnableVoiceOverAnnounce:(BOOL)enableVoiceOverAnnounce {
    _enableVoiceOverAnnounce = enableVoiceOverAnnounce;
    [self.webView evaluateJavaScript:[NSString stringWithFormat:@"term.setAccessibilityEnabled(%@)",
                                      enableVoiceOverAnnounce ? @"true" : @"false"]
                   completionHandler:nil];
}

- (NSDictionary *)sessionTraceAttributesWithByteCount:(NSInteger)byteCount
                                          pendingBefore:(NSInteger)pendingBefore {
    return @{
        @"attempt": @(self.attemptSequence),
        @"generation": @(self.sessionGeneration),
        @"ui_pid": @((int)getpid()),
        @"guest_pid": @(self.guestPID),
        @"is_restart_path": @(self.restartPath),
        @"has_terminal": @(self.hasSessionTerminal),
        @"first_pty_byte_seen": @(self.firstPTYByteSeen),
        @"byte_count": @(byteCount),
        @"pending_before": @(pendingBefore)
    };
}


- (int)sendOutput:(const void *)buf length:(int)len {
    // APPSIM-004 Stage 2: PTY byte detection
    // Record byte count at terminal input boundary
    NSDictionary *queueAttrs = [self sessionTraceAttributesWithByteCount:len
                                                             pendingBefore:_pendingData.length];
        if (!self.firstPTYByteSeen && len > 0) {
            self.firstPTYByteSeen = YES;
            [ISHInstrumentation recordEvent:@"session.pty.first_byte" attributes:queueAttrs];
            // Test-only bridge: notify the app process that the first PTY byte was seen.
            // UI tests observe this notification to know when the guest has produced
            // its first output byte and the terminal is no longer blank.
            [[NSNotificationCenter defaultCenter] postNotificationName:@"IXLand.TerminalFirstPTYByteSeenNotification" object:self];
        }
    [ISHInstrumentation recordEvent:@"terminal.output.queued" attributes:queueAttrs];
    [ISHInstrumentation recordEvent:@"pty.bytes.first_marker" attributes:queueAttrs];


    // Trace byte count at PTY master read boundary (TerminalView reading from PTY)
    NSDictionary *byteAttrs = @{
        @"byte_count": @(len),
        @"pending_before": @(_pendingData.length)
    };
    uint64_t ptyReadInterval = [ISHInstrumentation beginInterval:@"task.proof.pty.master.read" attributes:byteAttrs];
    
    if (len > 0) {
        NSString *queuedText = [[NSString alloc] initWithBytes:buf length:(NSUInteger) len encoding:NSISOLatin1StringEncoding];
        if (queuedText.length > 0) {
            @synchronized (self) {
                [self.testingTranscript appendString:queuedText];
                static const NSUInteger maxTranscriptLength = 16384;
                if (self.testingTranscript.length > maxTranscriptLength) {
                    NSRange keepRange = NSMakeRange(self.testingTranscript.length - maxTranscriptLength, maxTranscriptLength);
                    self.testingTranscript = [[self.testingTranscript substringWithRange:keepRange] mutableCopy];
                }
            }
            dispatch_async(dispatch_get_main_queue(), ^{
                self.contentChange = self.contentChange + 1;
            });
        }
    }

    lock(&_dataLock);
    if (!NSThread.isMainThread) {
        // The main thread is the only one that can unblock this, so sleeping here would be a deadlock.
        // The only reason for this to be called on the main thread is if input is echoed.
        while (_pendingData.length > BUF_SIZE)
            wait_for_ignore_signals(&_dataConsumed, &_dataLock, NULL);
    }
    [_pendingData appendData:[NSData dataWithBytes:buf length:len]];
    
    // Trace byte count after queuing
    NSDictionary *queuedAttrs = @{
        @"byte_count": @(len),
        @"pending_after": @(_pendingData.length)
    };
    [ISHInstrumentation endInterval:ptyReadInterval attributes:queuedAttrs];
    
    [self.refreshTask schedule];
    unlock(&_dataLock);
    return len;
}



- (void)sendInput:(NSData *)input {
    [ISHInstrumentation recordEvent:@"terminal.send_input.enter"];
    struct linux_tty *linuxTTY = nil;
    @synchronized (self) {
        linuxTTY = self.linuxTTY;
    }
    if (self.tty == NULL && linuxTTY == NULL)
        return;
    NSDictionary *inputAttrs = @{
        @"tty_ptr": @((uintptr_t)self.tty),
        @"byte_count": @(input.length),
        @"first_byte": input.length > 0 ? @(((const unsigned char *)input.bytes)[0]) : @(0)
    };
    uint64_t intervalId = [ISHInstrumentation beginInterval:@"terminal.send_input.data" attributes:inputAttrs];
    [ISHInstrumentation recordEvent:@"terminal.before_tty_input"];
    if (linuxTTY != NULL)
        linuxTTY->ops->send_input(linuxTTY, input.bytes, input.length);
    else
        tty_input(self.tty, input.bytes, input.length, 0);
    [ISHInstrumentation recordEvent:@"terminal.after_tty_input"];
    [ISHInstrumentation endInterval:intervalId attributes:inputAttrs];
    [self.webView evaluateJavaScript:@"exports.setUserGesture()" completionHandler:nil];
    [self.scrollToBottomTask schedule];
}

- (void)scrollToBottom {
    [self.webView evaluateJavaScript:@"exports.scrollToBottom()" completionHandler:nil];
}

- (NSString *)arrow:(char)direction {
    return [NSString stringWithFormat:@"\x1b%c%c", self.applicationCursor ? 'O' : '[', direction];
}

- (void)refresh {
    if (!self.loaded)
        return;
    
    // APPSIM-004 Stage 2: PTY byte detection
    [ISHInstrumentation recordEvent:@"terminal.refresh.triggered"
                         attributes:[self sessionTraceAttributesWithByteCount:0
                                                               pendingBefore:_pendingData.length]];

    lock(&_dataLock);
    if (_outputInProgress) {
        [self.refreshTask schedule];
        unlock(&_dataLock);
        return;
    }
    NSData *data = _pendingData;
    NSUInteger refreshByteCount = data.length;
    _pendingData = [[NSMutableData alloc] initWithCapacity:BUF_SIZE];
    _outputInProgress = YES;
    notify(&self->_dataConsumed);
    unlock(&_dataLock);

    // Trace byte count being sent to WebView
    __block uint64_t byteCountInterval = 0;
    if (refreshByteCount > 0) {
        NSDictionary *byteCountAttrs = @{
            @"byte_count": @(refreshByteCount),
            @"task": @"terminal_refresh",
            @"destination": @"webview"
        };
        byteCountInterval = [ISHInstrumentation beginInterval:@"task.proof.pty.byte_count" attributes:byteCountAttrs];
    }

    NSString *dataString = [[NSString alloc] initWithBytes:data.bytes length:data.length encoding:NSISOLatin1StringEncoding];
    // escape for javascript. only have to worry about the first 256 codepoints, because of the latin-1 encoding.
    dataString = [dataString stringByReplacingOccurrencesOfString:@"\\" withString:@"\\\\"];
    dataString = [dataString stringByReplacingOccurrencesOfString:@"\r" withString:@"\\r"];
    dataString = [dataString stringByReplacingOccurrencesOfString:@"\n" withString:@"\\n"];
    dataString = [dataString stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""];
    NSString *jsToEvaluate = [NSString stringWithFormat:@"exports.write(\"%@\")", dataString];
    if (refreshByteCount > 0) {
        [ISHInstrumentation recordEvent:@"terminal.js.write.bytes"
                             attributes:[self sessionTraceAttributesWithByteCount:refreshByteCount
                                                                   pendingBefore:_pendingData.length]];
    }
    [self.webView evaluateJavaScript:jsToEvaluate completionHandler:^(id result, NSError *error) {
        // Trace completion
        if (byteCountInterval != 0) {
            NSDictionary *completeAttrs = @{
                @"byte_count": @(refreshByteCount),
                @"error": error ? @YES : @NO
            };
            [ISHInstrumentation endInterval:byteCountInterval attributes:completeAttrs];
        }
        lock(&self->_dataLock);
        self->_outputInProgress = NO;
        unlock(&self->_dataLock);
        if (error != nil) {
            // Error handled silently - bytes could not be sent to terminal
            return;
        }
    }];
}

+ (void)convertCommand:(NSArray<NSString *> *)command toArgs:(char *)argv limitSize:(size_t)maxSize {
    char *p = argv;
    for (NSString *cmd in command) {
        const char *c = cmd.UTF8String;
        // Save space for the final NUL byte in argv
        while (p < argv + maxSize - 1 && (*p++ = *c++));
        // If we reach the end of the buffer, the last string still needs to be
        // NUL terminated
        *p = '\0';
    }
    // Add the final NUL byte to argv
    *++p = '\0';
}

+ (Terminal *)terminalWithType:(int)type number:(int)number {
    return [[Terminal alloc] initWithType:type number:number];
}

+ (Terminal *)terminalWithUUID:(NSUUID *)uuid {
    @synchronized (Terminal.class) {
        return [terminalsByUUID objectForKey:uuid];
    }
}

void Terminal_setLinuxTTY(nsobj_t _self, struct linux_tty *tty) {
    Terminal *self = (__bridge Terminal *) _self;
    @synchronized (self) {
        self.linuxTTY = tty;
    }
}

int Terminal_sendOutput_length(nsobj_t _self, const char *data, int size) {
    return [(__bridge Terminal *) _self sendOutput:data length:size];
}

int Terminal_roomForOutput(nsobj_t _self) {
    Terminal *self = (__bridge Terminal *) _self;
    lock(&self->_dataLock);
    int room = BUF_SIZE - (int) self.pendingData.length;
    unlock(&self->_dataLock);
    return room;
}

- (void)destroy {
    struct linux_tty *linuxTTY = nil;
    @synchronized (self) {
        linuxTTY = self.linuxTTY;
    }
    tty_t tty = self.tty;
    if (linuxTTY != NULL)
        linuxTTY->ops->hangup(linuxTTY);
    else if (tty != NULL) {
        lock(&tty->lock);
        tty_hangup(tty);
        unlock(&tty->lock);
    }
    @synchronized (Terminal.class) {
        [terminals removeObjectForKey:self.terminalsKey];
    }
}

// Test-only: Get terminal text for smoke tests
// Returns recently rendered terminal output without modifying runtime state
- (NSString *)screenTextForTesting {
    @synchronized (self) {
        return [self.testingTranscript copy] ?: @"";
    }
}

+ (void)initialize {
    if (self == Terminal.class) {
        terminals = [NSMapTable strongToWeakObjectsMapTable];
        terminalsByUUID = [NSMapTable strongToWeakObjectsMapTable];
    }
}

@end



static int ios_tty_init(struct tty *tty) {
    // This is called with ttys_lock but that results in deadlock since the main thread can also acquire ttys_lock. So release it.
    unlock(&ttys_lock);
    void (^init_block)(void) = ^{
        Terminal *terminal = [Terminal terminalWithType:tty->type number:tty->num];
        tty->data = (void *) CFBridgingRetain(terminal);
        terminal.tty = tty;
    };
    if ([NSThread isMainThread])
        init_block();
    else
        dispatch_sync(dispatch_get_main_queue(), init_block);

    lock(&ttys_lock);
    return 0;
}

static int ios_tty_write(struct tty *tty, const void *buf, size_t len, bool blocking) {
    Terminal *terminal = (__bridge Terminal *) tty->data;
    NSMutableDictionary *attrs = [[terminal sessionTraceAttributesWithByteCount:(NSInteger)len
                                                                   pendingBefore:0] mutableCopy];
    attrs[@"blocking"] = @(blocking);
    [ISHInstrumentation recordEvent:@"terminal.tty.write.callback" attributes:attrs];
    return [terminal sendOutput:buf length:(int) len];
}

static void ios_tty_cleanup(struct tty *tty) {
    Terminal *terminal = CFBridgingRelease(tty->data);
    tty->data = NULL;
    terminal.tty = NULL;
}

struct tty_driver_ops ios_tty_ops = {
    .init = ios_tty_init,
    .write = ios_tty_write,
    .cleanup = ios_tty_cleanup,
};
DEFINE_TTY_DRIVER(ios_console_driver, &ios_tty_ops, TTY_CONSOLE_MAJOR, 64);
struct tty_driver ios_pty_driver = {.ops = &ios_tty_ops};

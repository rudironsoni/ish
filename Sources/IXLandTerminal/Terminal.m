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
#import "IXLandTerminal-Swift.h"
#import "LinuxInterop.h"
#import <IXLandLinuxRuntime/fs/devices.h>
#import <IXLandLinuxRuntime/fs/tty.h>
#import <IXLandLinuxRuntime/fs/devices.h>
#import <IXLandLinuxRuntime/kernel/errno.h>

typedef struct tty *tty_t;

@interface Terminal () <IXLandGhosttyHostTerminalDelegate> {
    lock_t _dataLock;
    cond_t _dataConsumed;
}

@property (nonatomic) BOOL focusRequestedBeforeLoad;
@property BOOL loaded;
@property (nonatomic) tty_t tty;
@property (nonatomic, nullable) IXLandGhosttyHostTerminal *ghosttyTerminal;
// lock with dataLock for !linux and @synchronized(self) for linux
@property (nonatomic) NSMutableData *pendingData;
// sending output is an asynchronous thing due to terminal rendering, this is used to ensure it doesn't happen twice at once
@property (nonatomic) BOOL outputInProgress;

@property DelayedUITask *refreshTask;
@property DelayedUITask *scrollToBottomTask;

@property BOOL applicationCursor;
@property (nonatomic) NSUInteger contentChange;
@property (nonatomic) NSMutableString *testingTranscript;
@property (nonatomic) NSMutableData *pendingTranscriptControlBytes;
@property (nonatomic) NSMutableData *pendingGuestControlBytes;
@property (nonatomic) NSMutableData *pendingCursorEscapeBytes;
@property (nonatomic) NSInteger estimatedCursorRow;
@property (nonatomic) NSInteger estimatedCursorColumn;

@property NSNumber *terminalsKey;
@property NSUUID *uuid;
@property (nonatomic) struct linux_tty *linuxTTY;

@end

@implementation Terminal

static BOOL terminal_is_running_ui_tests(void) {
    NSDictionary *environment = NSProcessInfo.processInfo.environment;
    return environment[@"XCTestConfigurationFilePath"] != nil || environment[@"IXLAND_UI_TESTING"] != nil;
}
@synthesize webView = _webView;

static const int BUF_SIZE = 1<<14;
static const NSInteger IXLandMinimumUsableColumns = 20;
static const NSInteger IXLandMinimumUsableRows = 4;

static NSMapTable<NSNumber *, Terminal *> *terminals;
static NSMapTable<NSUUID *, Terminal *> *terminalsByUUID;

static NSData *terminal_filter_testing_transcript_bytes(NSMutableData *pendingBytes, NSData *data) {
    if (data.length == 0)
        return data;

    NSMutableData *scanData = [NSMutableData data];
    if (pendingBytes.length > 0) {
        [scanData appendData:pendingBytes];
        [pendingBytes setLength:0];
    }
    [scanData appendData:data];

    const uint8_t *bytes = scanData.bytes;
    NSUInteger length = scanData.length;
    NSMutableData *output = [NSMutableData dataWithCapacity:length];
    NSUInteger index = 0;
    while (index < length) {
        BOOL isCsi = bytes[index] == 0x9B;
        BOOL isEscCsi = bytes[index] == 0x1B && index + 1 < length && bytes[index + 1] == 0x5B;
        if (isCsi || isEscCsi) {
            NSUInteger cursor = index + (isCsi ? 1 : 2);
            while (cursor < length) {
                uint8_t finalByte = bytes[cursor];
                if (finalByte >= 0x40 && finalByte <= 0x7E)
                    break;
                cursor++;
            }
            if (cursor == length) {
                [pendingBytes appendBytes:&bytes[index] length:length - index];
                break;
            }

            // Only hide the exact device-status report request that pollutes
            // the transcript. Other CSI sequences must remain visible to avoid
            // swallowing adjacent printable output when escape sequences span
            // PTY read boundaries.
            BOOL isEscCsi6n = !isCsi && (cursor - index + 1) == 4 && bytes[index] == 0x1B &&
                bytes[index + 1] == 0x5B && bytes[index + 2] == 0x36 && bytes[index + 3] == 0x6E;
            BOOL isSingleCsi6n = isCsi && (cursor - index + 1) == 3 && bytes[index] == 0x9B &&
                bytes[index + 1] == 0x36 && bytes[index + 2] == 0x6E;
            if (isEscCsi6n || isSingleCsi6n) {
                index = cursor + 1;
                continue;
            }

            [output appendBytes:&bytes[index] length:cursor - index + 1];
            index = cursor + 1;
            continue;
        }

        [output appendBytes:&bytes[index] length:1];
        index++;
    }

    return output;
}

static NSString *terminal_control_sequence_string(const uint8_t *bytes, NSUInteger start,
                                                  NSUInteger end, BOOL isSingleCsi)
{
    NSUInteger payloadStart = start + (isSingleCsi ? 1 : 2);
    if (end < payloadStart)
        return @"";
    return [[NSString alloc] initWithBytes:&bytes[payloadStart]
                                    length:end - payloadStart + 1
                                  encoding:NSUTF8StringEncoding] ?: @"";
}

static void terminal_apply_csi_cursor_effect(Terminal *terminal, NSString *body)
{
    if (body.length == 0)
        return;

    unichar finalChar = [body characterAtIndex:body.length - 1];
    NSString *paramsText = [body substringToIndex:body.length - 1];
    NSArray<NSString *> *paramParts = [paramsText componentsSeparatedByString:@";"];
    NSMutableArray<NSNumber *> *params = [NSMutableArray arrayWithCapacity:paramParts.count];
    for (NSString *part in paramParts) {
        [params addObject:@(part.length == 0 ? 0 : part.intValue)];
    }

    NSInteger first = params.count > 0 ? MAX(1, params[0].integerValue) : 1;
    NSInteger second = params.count > 1 ? MAX(1, params[1].integerValue) : 1;

    switch (finalChar) {
    case 'A':
        terminal.estimatedCursorRow = MAX(1, terminal.estimatedCursorRow - first);
        break;
    case 'B':
    case 'e':
        terminal.estimatedCursorRow += first;
        break;
    case 'C':
    case 'a':
        terminal.estimatedCursorColumn += first;
        break;
    case 'D':
        terminal.estimatedCursorColumn = MAX(1, terminal.estimatedCursorColumn - first);
        break;
    case 'E':
        terminal.estimatedCursorRow += first;
        terminal.estimatedCursorColumn = 1;
        break;
    case 'F':
        terminal.estimatedCursorRow = MAX(1, terminal.estimatedCursorRow - first);
        terminal.estimatedCursorColumn = 1;
        break;
    case 'G':
        terminal.estimatedCursorColumn = first;
        break;
    case 'H':
    case 'f':
        terminal.estimatedCursorRow = first;
        terminal.estimatedCursorColumn = second;
        break;
    default:
        break;
    }
}

static void terminal_track_cursor_byte(Terminal *terminal, uint8_t byte)
{
    switch (byte) {
    case '\r':
        terminal.estimatedCursorColumn = 1;
        break;
    case '\n':
        terminal.estimatedCursorRow += 1;
        terminal.estimatedCursorColumn = 1;
        break;
    case '\b':
        terminal.estimatedCursorColumn = MAX(1, terminal.estimatedCursorColumn - 1);
        break;
    case '\t': {
        NSInteger nextTabStop = ((terminal.estimatedCursorColumn - 1) / 8 + 1) * 8 + 1;
        terminal.estimatedCursorColumn = MAX(1, nextTabStop);
        break;
    }
    default:
        if (byte >= 0x20 && byte != 0x7F)
            terminal.estimatedCursorColumn += 1;
        break;
    }
}

static NSData *terminal_filter_guest_output_bytes(Terminal *terminal, NSData *data)
{
    if (data.length == 0)
        return data;

    NSMutableData *scanData = [NSMutableData data];
    if (terminal.pendingGuestControlBytes.length > 0) {
        [scanData appendData:terminal.pendingGuestControlBytes];
        [terminal.pendingGuestControlBytes setLength:0];
    }
    [scanData appendData:data];

    const uint8_t *bytes = scanData.bytes;
    NSUInteger length = scanData.length;
    NSMutableData *output = [NSMutableData dataWithCapacity:length];
    NSUInteger index = 0;
    while (index < length) {
        BOOL isSingleCsi = bytes[index] == 0x9B;
        BOOL isEscCsi = bytes[index] == 0x1B && index + 1 < length && bytes[index + 1] == 0x5B;
        if (isSingleCsi || isEscCsi) {
            NSUInteger cursor = index + (isSingleCsi ? 1 : 2);
            while (cursor < length) {
                uint8_t finalByte = bytes[cursor];
                if (finalByte >= 0x40 && finalByte <= 0x7E)
                    break;
                cursor++;
            }
            if (cursor == length) {
                [terminal.pendingGuestControlBytes appendBytes:&bytes[index] length:length - index];
                break;
            }

            NSString *body =
                terminal_control_sequence_string(bytes, index, cursor, isSingleCsi);
            if ([body isEqualToString:@"[6n"]) {
                NSString *response =
                    [NSString stringWithFormat:@"\x1b[%ld;%ldR",
                                               (long) MAX(1, terminal.estimatedCursorRow),
                                               (long) MAX(1, terminal.estimatedCursorColumn)];
                NSData *responseData = [response dataUsingEncoding:NSUTF8StringEncoding];
                if (responseData.length > 0)
                    [terminal sendInput:responseData];
                index = cursor + 1;
                continue;
            }

            [output appendBytes:&bytes[index] length:cursor - index + 1];
            terminal_apply_csi_cursor_effect(terminal, body);
            index = cursor + 1;
            continue;
        }

        [output appendBytes:&bytes[index] length:1];
        terminal_track_cursor_byte(terminal, bytes[index]);
        index++;
    }

    return output;
}

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
            self.pendingTranscriptControlBytes = [NSMutableData data];
            self.pendingGuestControlBytes = [NSMutableData data];
            self.pendingCursorEscapeBytes = [NSMutableData data];
            self.estimatedCursorRow = 1;
            self.estimatedCursorColumn = 1;
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

- (UIView *)webView {
    if (_webView == nil) {
        double fontSize = UserPreferences.shared.fontSize.doubleValue;
        IXLandGhosttyHostTerminal *terminal = [[IXLandGhosttyHostTerminal alloc] initWithFontSize:fontSize];
        terminal.delegate = self;
        self.ghosttyTerminal = terminal;
        [self updateAppearanceStyle];
        _webView = terminal.view;
        self.loaded = YES;
        [self flushPendingFocus];
        [self refresh];
    }
    return _webView;
}

- (void)dealloc {
    self.ghosttyTerminal = nil;
}

+ (Terminal *)createPseudoTerminal:(struct tty **)tty {
    *tty = pty_open_guest_terminal(&terminal_pty_driver);
    if (IS_ERR(*tty))
        return nil;

    nsobj_t terminalObject = NULL;
    if (!Terminal_bindGuestTTY(*tty, &terminalObject)) {
        tty_release(*tty);
        *tty = ERR_PTR(_ENOMEM);
        return nil;
    }

    Terminal *terminal = (__bridge Terminal *)terminalObject;
    objc_put(terminalObject);
    return terminal;
}

- (void)setTty:(tty_t)tty {
    @synchronized (self) {
        _tty = tty;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        [self syncWindowSize];
    });
}

- (void)attachTTY:(struct tty *)tty {
    [self setTty:tty];
}

- (void)ghosttyHostTerminal:(IXLandGhosttyHostTerminal *)terminal didReceiveInput:(NSData *)data {
    [self sendInput:data];
}

- (void)ghosttyHostTerminal:(IXLandGhosttyHostTerminal *)terminal didResizeColumns:(NSInteger)columns rows:(NSInteger)rows {
    UIView *view = self.webView;
    CGSize size = view.bounds.size;
    NSInteger fallbackColumns = size.width >= 8 ? MAX(1, (NSInteger) (size.width / 8)) : 0;
    NSInteger fallbackRows = size.height >= 16 ? MAX(1, (NSInteger) (size.height / 16)) : 0;
    if (columns < IXLandMinimumUsableColumns && fallbackColumns >= IXLandMinimumUsableColumns) {
        columns = fallbackColumns;
    }
    if (rows < IXLandMinimumUsableRows && fallbackRows >= IXLandMinimumUsableRows) {
        rows = fallbackRows;
    }

    struct linux_tty *linuxTTY = nil;
    @synchronized (self) {
        linuxTTY = self.linuxTTY;
    }

    if (linuxTTY != NULL) {
        linuxTTY->ops->resize(linuxTTY, (int) columns, (int) rows);
        return;
    }

    if (self.tty == NULL)
        return;

    lock(&self.tty->lock);
    tty_set_winsize(self.tty, (struct winsize_) {.col = (uint16_t) columns, .row = (uint16_t) rows});
    unlock(&self.tty->lock);
}

- (void)syncWindowSize {
    if (!self.loaded)
        return;

    UIView *view = self.webView;
    CGSize size = view.bounds.size;
    if (size.width < 8 || size.height < 16)
        return;
    NSUInteger cols = MAX(1, (NSUInteger) (size.width / 8));
    NSUInteger rows = MAX(1, (NSUInteger) (size.height / 16));
    struct linux_tty *linuxTTY = nil;
    @synchronized (self) {
        linuxTTY = self.linuxTTY;
    }
    if (linuxTTY != NULL) {
        linuxTTY->ops->resize(linuxTTY, (int) cols, (int) rows);
        return;
    }
    if (self.tty == NULL)
        return;
    lock(&self.tty->lock);
    tty_set_winsize(self.tty, (struct winsize_) {.col = (uint16_t) cols, .row = (uint16_t) rows});
    unlock(&self.tty->lock);
}

- (void)syncWindowSizeIfPossible {
    if (![NSThread isMainThread]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self syncWindowSizeIfPossible];
        });
        return;
    }
    [self syncWindowSize];
}

- (BOOL)becomeInputResponder {
    return [self focusEditableSurface];
}

- (BOOL)requestFocus {
    return [self focusEditableSurface];
}

- (BOOL)focusEditableSurface {
    if (!self.webView || !self.webView.window || !self.loaded) {
        self.focusRequestedBeforeLoad = YES;
        return NO;
    }
    self.focusRequestedBeforeLoad = NO;
    return [self.ghosttyTerminal focus];
}

- (void)surfaceDidLayout {
    if (!self.loaded || self.ghosttyTerminal == nil)
        return;
    dispatch_async(dispatch_get_main_queue(), ^{
        [self.ghosttyTerminal surfaceDidLayout];
    });
}

- (void)updateFontSize:(CGFloat)fontSize {
    dispatch_async(dispatch_get_main_queue(), ^{
        [self.ghosttyTerminal updateFontSize:fontSize];
    });
}

- (void)updateAppearanceStyle {
    dispatch_async(dispatch_get_main_queue(), ^{
        Palette *palette = UserPreferences.shared.palette;
        [self.ghosttyTerminal updateAppearanceWithForegroundHex:palette.foregroundColor
                                                  backgroundHex:palette.backgroundColor
                                                      cursorHex:palette.cursorColor
                                               paletteOverrides:palette.colorPaletteOverrides
                                                 darkAppearance:UserPreferences.shared.requestingDarkAppearance];
    });
}

- (void)setEnableVoiceOverAnnounce:(BOOL)enableVoiceOverAnnounce {
    _enableVoiceOverAnnounce = enableVoiceOverAnnounce;
}

- (void)attachLinuxTTY:(struct linux_tty *)tty {
    @synchronized (self) {
        self.linuxTTY = tty;
    }
}

- (void)flushPendingFocus {
    if (!self.focusRequestedBeforeLoad)
        return;
    [self focusEditableSurface];
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
    NSData *rawOutput = [NSData dataWithBytes:buf length:(NSUInteger) len];
    NSData *filteredOutput = terminal_filter_guest_output_bytes(self, rawOutput);

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
    
    if (filteredOutput.length > 0) {
        NSData *transcriptBytes = terminal_filter_testing_transcript_bytes(self.pendingTranscriptControlBytes, filteredOutput);
        NSString *queuedText = [[NSString alloc] initWithData:transcriptBytes
                                                     encoding:NSISOLatin1StringEncoding];
        if (queuedText.length > 0) {
            @synchronized (self) {
                [self.testingTranscript appendString:queuedText];
                static const NSUInteger maxTranscriptLength = 16384;
                if (self.testingTranscript.length > maxTranscriptLength) {
                    NSRange keepRange = NSMakeRange(self.testingTranscript.length - maxTranscriptLength, maxTranscriptLength);
                    self.testingTranscript = [[self.testingTranscript substringWithRange:keepRange] mutableCopy];
                }
                if (terminal_is_running_ui_tests() && self.contentChange < 8) {
                    NSUInteger previewLength = MIN((NSUInteger) 80, self.testingTranscript.length);
                    NSUInteger previewStart = self.testingTranscript.length - previewLength;
                    NSString *preview = [self.testingTranscript substringFromIndex:previewStart];
                    [ISHInstrumentation recordEvent:@"terminal.testing_transcript.preview"
                                         attributes:@{ @"content_change": @(self.contentChange),
                                                       @"transcript_length": @(self.testingTranscript.length),
                                                       @"preview": preview ?: @"" }];
                }
                if (terminal_is_running_ui_tests()) {
                    NSUInteger previewLength = MIN((NSUInteger) 80, self.testingTranscript.length);
                    NSUInteger previewStart = self.testingTranscript.length - previewLength;
                    NSLog(@"[IXLandTranscript] bytes=%d transcript_length=%lu preview=%@",
                          len,
                          (unsigned long) self.testingTranscript.length,
                          [self.testingTranscript substringFromIndex:previewStart]);
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
    [_pendingData appendData:filteredOutput];
    BOOL shouldRefreshInline = [NSThread isMainThread] && filteredOutput.length > 0 && filteredOutput.length <= 64;
    
    // Trace byte count after queuing
    NSDictionary *queuedAttrs = @{
        @"byte_count": @(len),
        @"pending_after": @(_pendingData.length)
    };
    [ISHInstrumentation endInterval:ptyReadInterval attributes:queuedAttrs];

    unlock(&_dataLock);
    if (shouldRefreshInline) {
        // Echo and prompt bytes that are already on the main thread should not
        // wait for another runloop turn before painting.
        [self refresh];
    } else {
        [self.refreshTask schedule];
    }
    return len;
}



- (void)sendInput:(NSData *)input {
    if (input.length == 0)
        return;

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
    else if (self.tty->type == TTY_PSEUDO_MASTER_MAJOR && self.tty->driver->ops->write != NULL)
        self.tty->driver->ops->write(self.tty, input.bytes, input.length, false);
    else
        tty_input(self.tty, input.bytes, input.length, 0);
    [ISHInstrumentation recordEvent:@"terminal.after_tty_input"];
    [ISHInstrumentation endInterval:intervalId attributes:inputAttrs];
}

- (void)scrollToBottom {
}

- (NSString *)arrow:(char)direction {
    return [NSString stringWithFormat:@"\x1b%c%c", self.applicationCursor ? 'O' : '[', direction];
}

- (void)refresh {
    // APPSIM-004 Stage 2: PTY byte detection
    [ISHInstrumentation recordEvent:@"terminal.refresh.triggered"
                         attributes:[self sessionTraceAttributesWithByteCount:0
                                                               pendingBefore:_pendingData.length]];

    if (!self.loaded || self.ghosttyTerminal == nil)
        return;

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
            @"destination": @"ghostty"
        };
        byteCountInterval = [ISHInstrumentation beginInterval:@"task.proof.pty.byte_count" attributes:byteCountAttrs];
    }

    IXLandGhosttyHostTerminal *ghostty = self.ghosttyTerminal;
    if (refreshByteCount > 0) {
        if ([NSThread isMainThread]) {
            [ghostty receiveOutput:data];
        } else {
            dispatch_sync(dispatch_get_main_queue(), ^{
                [ghostty receiveOutput:data];
            });
        }
    }

    lock(&self->_dataLock);
    self->_outputInProgress = NO;
    unlock(&self->_dataLock);
    if (byteCountInterval != 0) {
        NSDictionary *completeAttrs = @{
            @"byte_count": @(refreshByteCount),
            @"error": @NO
        };
        [ISHInstrumentation endInterval:byteCountInterval attributes:completeAttrs];
    }
}

+ (void)convertCommand:(NSArray<NSString *> *)command toArgs:(char *)argv limitSize:(size_t)maxSize {
    if (maxSize == 0)
        return;

    char *p = argv;
    for (NSString *cmd in command) {
        const char *c = cmd.UTF8String;
        // Save space for the final NUL byte in argv
        while (p < argv + maxSize - 1 && (*p++ = *c++));
        // If we reach the end of the buffer, the last string still needs to be
        // NUL terminated
        *p = '\0';
    }
    // Leave an empty string immediately after the final argument so argv is
    // terminated with the expected double-NUL sentinel.
    *p = '\0';
}

+ (Terminal *)terminalWithType:(int)type number:(int)number {
    return [[Terminal alloc] initWithType:type number:number];
}

+ (Terminal *)terminalWithUUID:(NSUUID *)uuid {
    @synchronized (Terminal.class) {
        return [terminalsByUUID objectForKey:uuid];
    }
}

- (int)roomForOutput {
    lock(&_dataLock);
    int room = BUF_SIZE - (int) self.pendingData.length;
    unlock(&_dataLock);
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

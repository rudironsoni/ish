//
//  ViewController.m
//  iSH
//
//  Created by Theodore Dubois on 10/17/17.
//

#import "TerminalViewController.h"
#import "AppDelegate.h"
#import "TerminalView.h"
#import "BarButton.h"
#import "ArrowBarButton.h"
#import "UserPreferences.h"
#import "AboutViewController.h"
#import "CurrentRoot.h"
#import "Roots.h"
#import "root_registry.h"
#import "NSObject+SaneKVO.h"
#import "LinuxInterop.h"
#import "Instrumentation/ISHRuntimeFlags.h"
#import <ISHInstrumentation.h>
#import <IXLandLinuxRuntime/kernel/init.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/kernel/guest_trace_context.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/fs.h>
#import <IXLandLinuxRuntime/fs/devices.h>
#import <IXLandLinuxRuntime/emu/aarch64/cpu.h>
#import <IXLandLinuxRuntime/emu/tlb.h>
#import <IXLandInstrumentationTracing/trace.h>

#include <stdio.h>

static void trace_task_source_checkpoint(const char *name, struct task *task) {
    char pid_buf[32];
    char task_buf[32];
    char mm_buf[32];
    char mem_buf[32];
    char cpu_mmu_buf[32];
    char expected_mem_mmu_buf[32];

    snprintf(pid_buf, sizeof(pid_buf), "%u", (unsigned) (task ? task->pid : 0));
    snprintf(task_buf, sizeof(task_buf), "%p", (void *) task);
    snprintf(mm_buf, sizeof(mm_buf), "%p", task ? (void *) task->mm : NULL);
    snprintf(mem_buf, sizeof(mem_buf), "%p", task ? (void *) task->mem : NULL);
    snprintf(cpu_mmu_buf, sizeof(cpu_mmu_buf), "%p", task ? (void *) task->cpu.mmu : NULL);
    snprintf(expected_mem_mmu_buf, sizeof(expected_mem_mmu_buf), "%p",
             task && task->mem ? (void *) &task->mem->mmu : NULL);

    trace_attribute_t attrs[] = {
        {"pid", pid_buf},
        {"task", task_buf},
        {"mm", mm_buf},
        {"mem", mem_buf},
        {"cpu.mmu", cpu_mmu_buf},
        {"expected.mem.mmu", expected_mem_mmu_buf},
    };

    (void) trace_begin_interval(TRACE_ORIGIN_TASK, name, attrs, sizeof(attrs) / sizeof(attrs[0]));
}

static BOOL g_guestSessionActive = NO;
static NSString *const IXLandGuestSessionGuardLock = @"IXLandGuestSessionGuardLock";

static BOOL IXLandIsGuestSessionActive(void) {
    @synchronized (IXLandGuestSessionGuardLock) {
        return g_guestSessionActive;
    }
}

static void IXLandSetGuestSessionActive(BOOL active) {
    @synchronized (IXLandGuestSessionGuardLock) {
        g_guestSessionActive = active;
    }
}

#if ISH_RUNTIME_MODE_VALUE == 2
// APPSIM-004 Stage 3B: Trace stdio fd wiring state
// Captures what fd 0, 1, 2 point to and whether they're wired to PTY slave
static void trace_stdio_wiring_checkpoint(struct task *task) {
    // Always record entry to prove function is called
    [ISHInstrumentation recordEvent:@"stdio.fd0_target"];
    
    if (!task || !task->files)
        return;
    
    char pid_buf[32];
    char fd0_ptr[32] = "null";
    char fd1_ptr[32] = "null";
    char fd2_ptr[32] = "null";
    char fd0_tty[32] = "none";
    char fd1_tty[32] = "none";
    char fd2_tty[32] = "none";
    char fd0_major[32] = "0";
    char fd1_major[32] = "0";
    char fd2_major[32] = "0";
    char pty_slave_bound[32] = "0";
    char tty_session[32] = "0";
    
    snprintf(pid_buf, sizeof(pid_buf), "%u", (unsigned) task->pid);
    
    // Inspect fd table for fds 0, 1, 2
    lock(&task->files->lock);
    
    // fd 0
    if (task->files->size > 0 && task->files->files[0]) {
        struct fd *fd = task->files->files[0];
        snprintf(fd0_ptr, sizeof(fd0_ptr), "%p", (void *) fd);
        if (fd->tty) {
            snprintf(fd0_tty, sizeof(fd0_tty), "%p", (void *) fd->tty);
            snprintf(fd0_major, sizeof(fd0_major), "%d", fd->tty->type);
        }
    }
    
    // fd 1
    if (task->files->size > 1 && task->files->files[1]) {
        struct fd *fd = task->files->files[1];
        snprintf(fd1_ptr, sizeof(fd1_ptr), "%p", (void *) fd);
        if (fd->tty) {
            snprintf(fd1_tty, sizeof(fd1_tty), "%p", (void *) fd->tty);
            snprintf(fd1_major, sizeof(fd1_major), "%d", fd->tty->type);
        }
    }
    
    // fd 2
    if (task->files->size > 2 && task->files->files[2]) {
        struct fd *fd = task->files->files[2];
        snprintf(fd2_ptr, sizeof(fd2_ptr), "%p", (void *) fd);
        if (fd->tty) {
            snprintf(fd2_tty, sizeof(fd2_tty), "%p", (void *) fd->tty);
            snprintf(fd2_major, sizeof(fd2_major), "%d", fd->tty->type);
        }
    }
    
    // Check if fd 1 is bound to PTY slave (TTY_PSEUDO_SLAVE_MAJOR = 136)
    int pty_bound = 0;
    if (task->files->size > 1 && task->files->files[1]) {
        struct fd *fd = task->files->files[1];
        if (fd->tty && fd->tty->type == TTY_PSEUDO_SLAVE_MAJOR) {
            pty_bound = 1;
        }
    }
    snprintf(pty_slave_bound, sizeof(pty_slave_bound), "%d", pty_bound);
    
    // Check TTY session state
    int session_active = 0;
    if (task->group && task->group->tty) {
        session_active = 1;
    }
    snprintf(tty_session, sizeof(tty_session), "%d", session_active);
    
    unlock(&task->files->lock);
    
    // Emit stdio wiring checkpoint
    trace_attribute_t attrs[] = {
        {"pid", pid_buf},
        {"fd0.ptr", fd0_ptr},
        {"fd0.tty", fd0_tty},
        {"fd0.major", fd0_major},
        {"fd1.ptr", fd1_ptr},
        {"fd1.tty", fd1_tty},
        {"fd1.major", fd1_major},
        {"fd2.ptr", fd2_ptr},
        {"fd2.tty", fd2_tty},
        {"fd2.major", fd2_major},
        {"pty.slave.bound", pty_slave_bound},
        {"tty.session.active", tty_session},
    };
    
    (void) trace_begin_interval(TRACE_ORIGIN_TASK, "task.proof.stdio.wiring", attrs, sizeof(attrs) / sizeof(attrs[0]));
    
    // Record semantic events for each checkpoint
    [ISHInstrumentation recordEvent:@"stdio.fd0_target"];
    [ISHInstrumentation recordEvent:@"stdio.fd1_target"];
    [ISHInstrumentation recordEvent:@"stdio.fd2_target"];
    [ISHInstrumentation recordEvent:@"stdio.pty_slave_bound"];
    [ISHInstrumentation recordEvent:@"stdio.tty_session_state"];
}
#endif

@interface TerminalViewController ()

@property UITapGestureRecognizer *tapRecognizer;
@property (weak, nonatomic) IBOutlet TerminalView *termView;
@property (weak, nonatomic) IBOutlet NSLayoutConstraint *bottomConstraint;

@property (weak, nonatomic) IBOutlet UIButton *tabKey;
@property (weak, nonatomic) IBOutlet UIButton *controlKey;
@property (weak, nonatomic) IBOutlet UIButton *escapeKey;
@property (strong, nonatomic) IBOutletCollection(id) NSArray *barButtons;
@property (strong, nonatomic) IBOutletCollection(id) NSArray *barControls;

@property (weak, nonatomic) IBOutlet UIInputView *barView;
@property (weak, nonatomic) IBOutlet UIStackView *bar;
@property (weak, nonatomic) IBOutlet NSLayoutConstraint *barTop;
@property (weak, nonatomic) IBOutlet NSLayoutConstraint *barBottom;
@property (weak, nonatomic) IBOutlet NSLayoutConstraint *barLeading;
@property (weak, nonatomic) IBOutlet NSLayoutConstraint *barTrailing;
@property (weak, nonatomic) IBOutlet NSLayoutConstraint *barButtonWidth;
@property (weak, nonatomic) IBOutlet NSLayoutConstraint *barHeight;
@property (weak, nonatomic) IBOutlet UIView *settingsBadge;

@property (weak, nonatomic) IBOutlet UIButton *infoButton;
@property (weak, nonatomic) IBOutlet UIButton *pasteButton;
@property (weak, nonatomic) IBOutlet UIButton *hideKeyboardButton;

@property int sessionPid;
@property (nonatomic) Terminal *sessionTerminal;
@property (nonatomic) uint64_t sessionAttemptSequence;
@property (nonatomic) uint64_t lastExitedAttemptSequence;
@property (nonatomic) uint64_t sessionGenerationSequence;
@property (nonatomic) uint64_t activeSessionGeneration;
@property (nonatomic) uint64_t lastHandledExitGeneration;
@property (nonatomic) BOOL sessionStartInProgress;
@property (nonatomic) CFAbsoluteTime activeSessionStartTime;
@property (nonatomic) NSInteger postPTYRestartBudget;
@property (nonatomic, copy) NSString *lastSessionFailureLabel;
@property (nonatomic, copy) NSString *lastSessionFailingCall;
@property (nonatomic) int lastSessionFailureReturnValue;
@property (nonatomic, copy) NSString *lastSessionFailurePath;
@property (nonatomic, copy) NSString *lastSessionFailurePTYDevicePath;
@property (nonatomic) BOOL lastSessionFailureMountsNonEmpty;
@property (nonatomic, copy) NSString *lastSessionFailureRootPath;
@property (nonatomic) int lastRootMountReturnValue;
@property (nonatomic) int lastBecomeNewInitChildReturnValue;
@property (nonatomic) int lastPTYCreationReturnValue;
@property (nonatomic) int lastSTDIOCreateReturnValue;
@property (nonatomic) int lastLoginExecReturnValue;
@property (nonatomic) BOOL lastTaskStartEntered;
@property (nonatomic, copy) NSString *lastTaskStartReturnText;

// Controller should not create window-level synthetic accessibility
// elements. TerminalView is the honest owner of the TerminalSurface proxy.

@property BOOL ignoreKeyboardMotion;
@property (nonatomic) BOOL hasExternalKeyboard;

@end

@implementation TerminalViewController

- (NSDictionary *)sessionAttemptAttributesWithExtra:(NSDictionary *)extra {
    NSMutableDictionary *attributes = [NSMutableDictionary dictionary];
    attributes[@"attempt"] = @(self.sessionAttemptSequence);
    attributes[@"generation"] = @(self.activeSessionGeneration);
    attributes[@"ui_pid"] = @((int)getpid());
    attributes[@"session_pid"] = @(self.sessionPid);
    attributes[@"guest_pid"] = @(self.sessionPid);
    attributes[@"has_terminal"] = @(self.sessionTerminal != nil);
    attributes[@"is_restart_path"] = @((self.sessionTerminal != nil && self.sessionTerminal.restartPath) || self.lastExitedAttemptSequence != 0);
    attributes[@"first_pty_byte_seen"] = @(self.sessionTerminal.firstPTYByteSeen);
    attributes[@"after_process_exit_attempt"] = @(self.lastExitedAttemptSequence);
    if (self.activeSessionStartTime > 0) {
        CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
        attributes[@"uptime_ms"] = @((long long)((now - self.activeSessionStartTime) * 1000.0));
    }
    if (self.sessionTerminal.uuid != nil)
        attributes[@"terminal_uuid"] = self.sessionTerminal.uuid.UUIDString;
    if (extra != nil)
        [attributes addEntriesFromDictionary:extra];
    return attributes;
}

- (void)recordSessionAttemptEvent:(NSString *)name extra:(NSDictionary *)extra {
    [ISHInstrumentation recordEvent:name attributes:[self sessionAttemptAttributesWithExtra:extra]];
}

- (void)recordSessionStartupFailureLabel:(NSString *)label
                             failingCall:(NSString *)failingCall
                             returnValue:(int)returnValue
                                    path:(NSString *)path
                           ptyDevicePath:(NSString *)ptyDevicePath
                         mountsNonEmpty:(BOOL)mountsNonEmpty {
    self.lastSessionFailureLabel = label;
    self.lastSessionFailingCall = failingCall;
    self.lastSessionFailureReturnValue = returnValue;
    self.lastSessionFailurePath = path ?: @"";
    self.lastSessionFailurePTYDevicePath = ptyDevicePath ?: @"";
    self.lastSessionFailureMountsNonEmpty = mountsNonEmpty;
    [self recordSessionAttemptEvent:@"session.failure.owner"
                              extra:@{ @"failure_label": label ?: @"",
                                       @"failing_call": failingCall ?: @"",
                                       @"return_value": @(returnValue),
                                       @"path": path ?: @"",
                                       @"pty_device_path": ptyDevicePath ?: @"",
                                       @"mounts_non_empty": @(mountsNonEmpty),
                                       @"root_path": self.lastSessionFailureRootPath ?: @"",
                                       @"root_mount_return_value": @(self.lastRootMountReturnValue),
                                       @"become_new_init_child_return_value": @(self.lastBecomeNewInitChildReturnValue),
                                       @"pty_create_return_value": @(self.lastPTYCreationReturnValue),
                                       @"stdio_create_return_value": @(self.lastSTDIOCreateReturnValue),
                                       @"login_exec_return_value": @(self.lastLoginExecReturnValue),
                                       @"task_start_entered": @(self.lastTaskStartEntered) }];
}

- (NSString *)sessionFailureSubtitleForReturnValue:(int)returnValue {
    NSMutableArray<NSString *> *lines = [NSMutableArray array];
    NSString *label = self.lastSessionFailureLabel.length > 0 ? self.lastSessionFailureLabel : @"unknown";
    NSString *call = self.lastSessionFailingCall.length > 0 ? self.lastSessionFailingCall : @"unknown";
    NSString *rootPath = self.lastSessionFailureRootPath.length > 0 ? self.lastSessionFailureRootPath : @"unknown";
    NSString *path = self.lastSessionFailurePath.length > 0 ? self.lastSessionFailurePath : @"unknown";
    NSString *ptyPath = self.lastSessionFailurePTYDevicePath.length > 0 ? self.lastSessionFailurePTYDevicePath : @"unknown";
    NSString *taskStartRet = self.lastTaskStartReturnText.length > 0 ? self.lastTaskStartReturnText : @"unknown";

    [lines addObject:[NSString stringWithFormat:@"label=%@", label]];
    [lines addObject:[NSString stringWithFormat:@"call=%@", call]];
    [lines addObject:[NSString stringWithFormat:@"ret=%d", returnValue]];
    [lines addObject:[NSString stringWithFormat:@"path=%@", path]];
    [lines addObject:[NSString stringWithFormat:@"root_path=%@", rootPath]];
    [lines addObject:[NSString stringWithFormat:@"root_present=%@", [AppDelegate lastBootstrapRootPresent] ? @"true" : @"false"]];
    [lines addObject:[NSString stringWithFormat:@"root_exists=%@", [AppDelegate lastBootstrapRootExists] ? @"true" : @"false"]];
    [lines addObject:[NSString stringWithFormat:@"root_data_exists=%@", [AppDelegate lastBootstrapRootDataExists] ? @"true" : @"false"]];
    [lines addObject:[NSString stringWithFormat:@"roots_available=%@", [AppDelegate lastBootstrapRootsAvailable] ? @"true" : @"false"]];
    [lines addObject:[NSString stringWithFormat:@"archive_url_present=%@", [AppDelegate lastBootstrapArchiveURLPresent] ? @"true" : @"false"]];
    [lines addObject:[NSString stringWithFormat:@"import_attempted=%@", [AppDelegate lastBootstrapImportAttempted] ? @"true" : @"false"]];
    [lines addObject:[NSString stringWithFormat:@"import_succeeded=%@", [AppDelegate lastBootstrapImportSucceeded] ? @"true" : @"false"]];
    NSString *importError = [AppDelegate lastBootstrapImportErrorDescription];
    [lines addObject:[NSString stringWithFormat:@"import_error=%@", importError.length > 0 ? importError : @"unknown"]];
    [lines addObject:[NSString stringWithFormat:@"root_mount_called=%@", [AppDelegate lastBootstrapMountRootCalled] ? @"true" : @"false"]];
    [lines addObject:[NSString stringWithFormat:@"root_mount_ret=%d", self.lastRootMountReturnValue]];
    [lines addObject:[NSString stringWithFormat:@"boot_become_first_process_called=%@", [AppDelegate lastBootstrapBecomeFirstProcessCalled] ? @"true" : @"false"]];
    [lines addObject:[NSString stringWithFormat:@"boot_become_first_process_ret=%d", [AppDelegate lastBecomeFirstProcessReturnValue]]];
    [lines addObject:[NSString stringWithFormat:@"boot_pid1_exists_after_become_first_process=%@", [AppDelegate lastBootstrapPID1ExistsAfterBecomeFirstProcess] ? @"true" : @"false"]];
    [lines addObject:[NSString stringWithFormat:@"boot_mounts_non_empty_after_root_mount=%@", [AppDelegate lastMountsNonEmptyAfterRootMount] ? @"true" : @"false"]];
    [lines addObject:[NSString stringWithFormat:@"mounts_non_empty=%@", self.lastSessionFailureMountsNonEmpty ? @"true" : @"false"]];
    [lines addObject:[NSString stringWithFormat:@"become_new_init_child_ret=%d", self.lastBecomeNewInitChildReturnValue]];
    [lines addObject:[NSString stringWithFormat:@"pty_create_ret=%d", self.lastPTYCreationReturnValue]];
    [lines addObject:[NSString stringWithFormat:@"pty_device_path=%@", ptyPath]];
    [lines addObject:[NSString stringWithFormat:@"stdio_create_ret=%d", self.lastSTDIOCreateReturnValue]];
    [lines addObject:[NSString stringWithFormat:@"login_exec_ret=%d", self.lastLoginExecReturnValue]];
    [lines addObject:[NSString stringWithFormat:@"task_start_entered=%@", self.lastTaskStartEntered ? @"true" : @"false"]];
    [lines addObject:[NSString stringWithFormat:@"task_start_ret=%@", taskStartRet]];
    return [lines componentsJoinedByString:@"\n"];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    [ISHInstrumentation recordEvent:@"terminal.accessibility.controller.viewDidLoad.enter"];
    self.postPTYRestartBudget = 1;

    NSURL *root = ios_root_default_url();
    self.lastSessionFailureRootPath = root.path ?: @"";
    self.lastRootMountReturnValue = [AppDelegate lastRootMountReturnValue];
    self.lastTaskStartReturnText = @"unknown";

    int bootError = [AppDelegate bootError];
    if (bootError < 0) {
        self.lastSessionFailureLabel = root == nil ? @"root_missing" : @"root_mount_failed";
        self.lastSessionFailingCall = root == nil ? @"rootUrl" : @"mount_root";
        self.lastSessionFailureReturnValue = bootError;
        self.lastSessionFailureMountsNonEmpty = mounts_is_non_empty();
        NSString *message = [NSString stringWithFormat:@"could not boot"];
        NSString *subtitle = [self sessionFailureSubtitleForReturnValue:bootError];
        if (bootError == _EINVAL)
            subtitle = [subtitle stringByAppendingString:@"\n(try reinstalling the app, see release notes for details)"];
        [self showMessage:message subtitle:subtitle];
    }

    self.terminal = self.terminal;
    
    // The controller should not become an accessibility element that hides
    // children. Let TerminalView provide the TerminalSurface accessibility
    // proxy which XCUI can discover.
    self.view.isAccessibilityElement = NO;
    self.view.accessibilityIdentifier = @"TerminalViewController";
    
    self.tapRecognizer = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(handleTerminalTap:)];
    self.tapRecognizer.cancelsTouchesInView = NO;
    [self.view addGestureRecognizer:self.tapRecognizer];

    self.bottomConstraint.constant = 0;

    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    [center addObserver:self
               selector:@selector(_updateBadge)
                   name:FsUpdatedNotification
                 object:nil];


    [self _updateStyleFromPreferences:NO];
    
    if (UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad) {
        [self.bar removeArrangedSubview:self.hideKeyboardButton];
        [self.hideKeyboardButton removeFromSuperview];
    }
    if (UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPhone) {
        self.barHeight.constant = 36;
    } else {
        self.barHeight.constant = 43;
    }
    
    // SF Symbols is cool
    if (@available(iOS 13, *)) {
        [self.infoButton setImage:[UIImage systemImageNamed:@"gear"] forState:UIControlStateNormal];
        [self.pasteButton setImage:[UIImage systemImageNamed:@"doc.on.clipboard"] forState:UIControlStateNormal];
        [self.hideKeyboardButton setImage:[UIImage systemImageNamed:@"keyboard.chevron.compact.down"] forState:UIControlStateNormal];
        
        [self.tabKey setTitle:nil forState:UIControlStateNormal];
        [self.tabKey setImage:[UIImage systemImageNamed:@"arrow.right.to.line.alt"] forState:UIControlStateNormal];
        [self.controlKey setTitle:nil forState:UIControlStateNormal];
        [self.controlKey setImage:[UIImage systemImageNamed:@"control"] forState:UIControlStateNormal];
        [self.escapeKey setTitle:nil forState:UIControlStateNormal];
        [self.escapeKey setImage:[UIImage systemImageNamed:@"escape"] forState:UIControlStateNormal];
    }
    
    [UserPreferences.shared observe:@[@"hideStatusBar"] options:0 owner:self usingBlock:^(typeof(self) self) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self setNeedsStatusBarAppearanceUpdate];
        });
    }];
    [UserPreferences.shared observe:@[@"colorScheme", @"theme", @"hideExtraKeysWithExternalKeyboard"]
                            options:0 owner:self usingBlock:^(typeof(self) self) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self _updateStyleFromPreferences:YES];
        });
    }];
    [self _updateBadge];

    // No window-level synthetic accessibility proxy should be created by the
    // controller. TerminalView owns the TerminalSurface proxy.
}

- (void)awakeFromNib {
    [super awakeFromNib];
    [NSNotificationCenter.defaultCenter addObserver:self
                                           selector:@selector(processExited:)
                                               name:ProcessExitedNotification
                                             object:nil];
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    dispatch_async(dispatch_get_main_queue(), ^{
        [self focusTerminalInput];
    });
}

- (void)handleTerminalTap:(UITapGestureRecognizer *)recognizer {
    if (recognizer.state == UIGestureRecognizerStateEnded) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self focusTerminalInput];
        });
    }
}

- (BOOL)isRunningUITests {
    NSDictionary *environment = NSProcessInfo.processInfo.environment;
    return environment[@"XCTestConfigurationFilePath"] != nil || environment[@"IXLAND_UI_TESTING"] != nil;
}

- (BOOL)focusTerminalInput {
    if ([self isRunningUITests]) {
        return [self.termView focusForTesting];
    }
    if (self.terminal != nil && [self.terminal requestFocus]) {
        return YES;
    }
    return [self.termView becomeFirstResponder];
}

- (void)startNewSession {
    BOOL isRestartPath = (self.lastExitedAttemptSequence != 0);
    [self recordSessionAttemptEvent:@"session.start.requested" extra:@{ @"is_restart_path": @(isRestartPath), @"post_pty_restart_budget": @(self.postPTYRestartBudget) }];
    if (self.sessionStartInProgress) {
        [self recordSessionAttemptEvent:@"session.start.blocked.reentrant" extra:@{ @"is_restart_path": @(isRestartPath) }];
        return;
    }
    if (ISH_RUNTIME_MODE_VALUE == 3 && IXLandIsGuestSessionActive()) {
        [self recordSessionAttemptEvent:@"session.start.blocked.active_guest" extra:@{ @"is_restart_path": @(isRestartPath) }];
        return;
    }

    self.sessionStartInProgress = YES;
    self.sessionPid = 0;
    self.sessionAttemptSequence += 1;
    self.sessionGenerationSequence += 1;
    self.activeSessionGeneration = self.sessionGenerationSequence;
    self.activeSessionStartTime = CFAbsoluteTimeGetCurrent();
    [self recordSessionAttemptEvent:@"session.generation.assigned" extra:@{ @"is_restart_path": @(isRestartPath), @"assigned_generation": @(self.activeSessionGeneration) }];
    [self recordSessionAttemptEvent:@"session.start.enter" extra:@{ @"is_restart_path": @(isRestartPath) }];

    self.lastSessionFailureLabel = nil;
    self.lastSessionFailingCall = nil;
    self.lastSessionFailureReturnValue = 0;
    self.lastSessionFailurePath = @"";
    self.lastSessionFailurePTYDevicePath = @"";
    self.lastSessionFailureMountsNonEmpty = mounts_is_non_empty();
    self.lastBecomeNewInitChildReturnValue = 0;
    self.lastPTYCreationReturnValue = 0;
    self.lastSTDIOCreateReturnValue = 0;
    self.lastLoginExecReturnValue = 0;
    self.lastTaskStartEntered = NO;
    self.lastTaskStartReturnText = @"unknown";

    int err = [self startSession];
    [self recordSessionAttemptEvent:@"session.start.exit" extra:@{ @"is_restart_path": @(isRestartPath), @"return_value": @(err) }];
    // Minimal semantic probe to make the startSession return visible in traces
    // for quick diagnosis of which bootstrap sub-step failed.
    [ISHInstrumentation recordEvent:@"session.start.exit.detail" attributes:@{ @"is_restart_path": @(isRestartPath), @"return_value": @(err), @"runtime_mode": @(ISH_RUNTIME_MODE_VALUE) }];
    self.sessionStartInProgress = NO;

    if (err < 0) {
        NSString *subtitle = [self sessionFailureSubtitleForReturnValue:err];
        [self recordSessionAttemptEvent:@"session.dialog.raise" extra:@{ @"return_value": @(err), @"message": @"could not start session", @"subtitle": subtitle ?: @"", @"is_restart_path": @(isRestartPath) }];
        [self showMessage:@"could not start session"
                 subtitle:subtitle];
    }
}

- (void)reconnectSessionFromTerminalUUID:(NSUUID *)uuid {
    self.sessionTerminal = [Terminal terminalWithUUID:uuid];
    if (self.sessionTerminal == nil)
        [self startNewSession];
}

- (NSUUID *)sessionTerminalUUID {
    return self.terminal.uuid;
}

- (int)startSession {
    [self recordSessionAttemptEvent:@"session.attempt.startSession.entry" extra:nil];
    NSURL *root = ios_root_default_url();
    self.lastSessionFailureRootPath = root.path ?: @"";
    NSArray<NSString *> *command = UserPreferences.shared.launchCommand;

    // Shell-only mode: Skip ALL session infrastructure
    // No PTY, no Terminal, no become_new_init_child, no stdio setup
    // Just record the event and return success
#if ISH_RUNTIME_MODE_VALUE == 0
    {
        [ISHInstrumentation recordEvent:@"session.bootstrap.deferred"];
        // Mark session as deferred/inactive - no guest running, no terminal
        self.sessionPid = -1;
        // Terminal remains nil - UI shows empty terminal view
        self.sessionTerminal = nil;
        // Return success without creating any session infrastructure
        return 0;
    }
#endif

    // Session bootstrap mode: Init child + PTY + stdio, but NO exec/start
    // Creates session infrastructure without guest execution
#if ISH_RUNTIME_MODE_VALUE == 1
    {
        // Step 1: Become init child
        int err = become_new_init_child();
        if (err < 0)
            return err;

        // Step 2: Create PTY
        struct tty *tty;
        self.sessionTerminal = nil;
        Terminal *terminal = [Terminal createPseudoTerminal:&tty];
        if (terminal == nil) {
            NSAssert(IS_ERR(tty), @"tty should be error");
            return (int) PTR_ERR(tty);
        }
        self.sessionTerminal = terminal;

        // Step 3: Setup stdio
        NSString *stdioFile = [NSString stringWithFormat:@"/dev/pts/%d", tty->num];
        err = create_stdio(stdioFile.fileSystemRepresentation, TTY_PSEUDO_SLAVE_MAJOR, tty->num);
        if (err < 0)
            return err;
        tty_release(tty);

        // Step 4: Skip do_execve() - guest execution deferred
        // Step 5: Skip task_start() - guest execution deferred

        // Mark session as bootstrap done, exec deferred
        self.sessionPid = -2;

        // Record semantic event
        [ISHInstrumentation recordEvent:@"session.bootstrap.ready"];

        return 0;
    }
#endif

    // Session exec mode: Init child + PTY + stdio + do_execve, but NO task_start
    // Loads shell binary without starting guest execution
#if ISH_RUNTIME_MODE_VALUE == 2
    {
        // Step 1: Become init child
        int err = become_new_init_child();
        if (err < 0)
            return err;
        trace_task_source_checkpoint("task.proof.after_become_new_init_child", current);

        // Step 2: Create PTY
        struct tty *tty;
        self.sessionTerminal = nil;
        Terminal *terminal = [Terminal createPseudoTerminal:&tty];
        if (terminal == nil) {
            NSAssert(IS_ERR(tty), @"tty should be error");
            return (int) PTR_ERR(tty);
        }
        self.sessionTerminal = terminal;

        // Step 3: Setup stdio
        NSString *stdioFile = [NSString stringWithFormat:@"/dev/pts/%d", tty->num];
        err = create_stdio(stdioFile.fileSystemRepresentation, TTY_PSEUDO_SLAVE_MAJOR, tty->num);
        if (err < 0)
            return err;
        trace_task_source_checkpoint("task.proof.after_create_stdio", current);
        
        // APPSIM-004 Stage 3B: Prove stdio wiring for fd 0, 1, 2
        // Capture what fds point to and whether they're wired to PTY slave
        [ISHInstrumentation recordEvent:@"stdio.fd0_target"];
        trace_stdio_wiring_checkpoint(current);
        
        tty_release(tty);

        // Step 4: Call do_execve() - load shell binary
        char argv[4096];
        [Terminal convertCommand:command toArgs:argv limitSize:sizeof(argv)];
        const char *envp = "TERM=xterm-256color\0";
        
        // APPSIM-004 Stage 3A: Trace exec entry
        [ISHInstrumentation recordEvent:@"login.exec.entry"];
        trace_task_source_checkpoint("task.proof.login.exec.entry", current);
        
        err = do_execve(command[0].UTF8String, command.count, argv, envp);
        
        if (err < 0) {
            // APPSIM-004 Stage 3A: Trace exec failure
            [ISHInstrumentation recordEvent:@"login.exec.failure"];
            trace_task_source_checkpoint("task.proof.login.exec.failure", current);
            return err;
        }
        
        // APPSIM-004 Stage 3A: Trace exec success
        [ISHInstrumentation recordEvent:@"login.exec.success"];
        trace_task_source_checkpoint("task.proof.login.exec.success", current);
        
        // APPSIM-004 Stage 3A: Verify PID remains alive after exec
        if (current != NULL && current->pid != 0) {
            [ISHInstrumentation recordEvent:@"login.pid_alive"];
            trace_task_source_checkpoint("task.proof.login.pid.alive", current);
        }

        // Step 5: Skip task_start() - guest execution deferred
        // Exec succeeded, record PID but no task running
        self.sessionPid = current->pid;

        // Record semantic event
        [ISHInstrumentation recordEvent:@"session.exec.ready"];

        return 0;
    }
#endif

    // Full-guest mode: Normal session creation with PTY and Terminal
    BOOL isRestartPath = (self.lastExitedAttemptSequence != 0);
    self.terminal = nil;
    self.sessionTerminal = nil;
    [ISHInstrumentation recordEvent:@"app.session.start.enter"
                         attributes:@{ @"is_restart_path": @(isRestartPath),
                                       @"command_path": command.firstObject ?: @"",
                                       @"mounts_non_empty": @(mounts_is_non_empty()) }];
    [ISHInstrumentation recordEvent:@"app.session.preflight.mounts.non_empty"
                         attributes:@{ @"mounts_non_empty": @(mounts_is_non_empty()),
                                       @"is_restart_path": @(isRestartPath) }];

    BOOL mountsNonEmptyBeforeSession = mounts_is_non_empty();
    if (!mountsNonEmptyBeforeSession || pid_get_task(1) == NULL) {
        int bootstrapErr = [AppDelegate bootstrapRuntimeForSession];
        NSURL *bootstrapRoot = ios_root_default_url();
        self.lastSessionFailureRootPath = bootstrapRoot.path ?: @"";
        self.lastRootMountReturnValue = [AppDelegate lastRootMountReturnValue];
        if (bootstrapErr < 0) {
            self.lastBecomeNewInitChildReturnValue = bootstrapErr;
            [self recordSessionStartupFailureLabel:@"runtime_bootstrap_failed"
                                       failingCall:@"bootstrapRuntimeForSession"
                                       returnValue:bootstrapErr
                                              path:self.lastSessionFailureRootPath
                                     ptyDevicePath:nil
                                   mountsNonEmpty:mounts_is_non_empty()];
            [ISHInstrumentation recordEvent:@"app.session.failure"
                                 attributes:@{ @"failing_call": @"bootstrapRuntimeForSession",
                                               @"return_value": @(bootstrapErr),
                                               @"errno_style_code": @(bootstrapErr),
                                               @"is_restart_path": @(isRestartPath),
                                               @"mounts_non_empty": @(mounts_is_non_empty()),
                                               @"root_mount_return_value": @([AppDelegate lastRootMountReturnValue]),
                                               @"boot_become_first_process_return_value": @([AppDelegate lastBecomeFirstProcessReturnValue]),
                                               @"boot_mounts_non_empty_after_root_mount": @([AppDelegate lastMountsNonEmptyAfterRootMount]) }];
            return bootstrapErr;
        }
        mountsNonEmptyBeforeSession = mounts_is_non_empty();
        if (!mountsNonEmptyBeforeSession) {
            int err = _ENODEV;
            self.lastBecomeNewInitChildReturnValue = err;
            [self recordSessionStartupFailureLabel:@"runtime_bootstrap_postcondition_failed"
                                       failingCall:@"mounts_is_non_empty"
                                       returnValue:err
                                              path:self.lastSessionFailureRootPath
                                     ptyDevicePath:nil
                                   mountsNonEmpty:NO];
            [ISHInstrumentation recordEvent:@"app.session.failure"
                                 attributes:@{ @"failing_call": @"mounts_is_non_empty",
                                               @"return_value": @(err),
                                               @"errno_style_code": @(err),
                                               @"is_restart_path": @(isRestartPath),
                                               @"mounts_non_empty": @NO,
                                               @"root_mount_return_value": @([AppDelegate lastRootMountReturnValue]),
                                               @"boot_become_first_process_return_value": @([AppDelegate lastBecomeFirstProcessReturnValue]),
                                               @"boot_mounts_non_empty_after_root_mount": @([AppDelegate lastMountsNonEmptyAfterRootMount]) }];
            return err;
        }
    }
    [ISHInstrumentation recordEvent:@"app.session.become_new_init_child.enter"
                         attributes:@{ @"is_restart_path": @(isRestartPath),
                                       @"mounts_non_empty": @(mountsNonEmptyBeforeSession) }];
    if (!mountsNonEmptyBeforeSession) {
        int err = _ENODEV;
        self.lastBecomeNewInitChildReturnValue = err;
        [self recordSessionStartupFailureLabel:@"mounts_empty_before_session"
                                   failingCall:@"mounts_is_non_empty"
                                   returnValue:err
                                          path:self.lastSessionFailureRootPath
                                 ptyDevicePath:nil
                               mountsNonEmpty:NO];
        [ISHInstrumentation recordEvent:@"app.session.failure"
                             attributes:@{ @"failing_call": @"mounts_is_non_empty",
                                           @"return_value": @(err),
                                           @"errno_style_code": @(err),
                                           @"is_restart_path": @(isRestartPath),
                                           @"mounts_non_empty": @NO,
                                           @"root_mount_return_value": @([AppDelegate lastRootMountReturnValue]),
                                           @"boot_become_first_process_return_value": @([AppDelegate lastBecomeFirstProcessReturnValue]),
                                           @"boot_mounts_non_empty_after_root_mount": @([AppDelegate lastMountsNonEmptyAfterRootMount]) }];
        [self recordSessionAttemptEvent:@"session.attempt.fail.mounts_empty_before_session"
                                  extra:@{ @"return_value": @(err),
                                           @"is_restart_path": @(isRestartPath) }];
        return err;
    }
    int err = become_new_init_child();
    self.lastBecomeNewInitChildReturnValue = err;
    [ISHInstrumentation recordEvent:@"app.session.become_new_init_child.exit"
                         attributes:@{ @"return_value": @(err),
                                       @"errno_style_code": @(err),
                                       @"is_restart_path": @(isRestartPath),
                                       @"mounts_non_empty": @(mounts_is_non_empty()) }];
    if (err < 0) {
        [self recordSessionStartupFailureLabel:@"become_new_init_child_failed"
                                   failingCall:@"become_new_init_child"
                                   returnValue:err
                                          path:command.firstObject
                                 ptyDevicePath:nil
                               mountsNonEmpty:mounts_is_non_empty()];
        [ISHInstrumentation recordEvent:@"app.session.failure"
                             attributes:@{ @"failing_call": @"become_new_init_child",
                                           @"return_value": @(err),
                                           @"errno_style_code": @(err),
                                           @"is_restart_path": @(isRestartPath),
                                           @"mounts_non_empty": @(mounts_is_non_empty()) }];
        [self recordSessionAttemptEvent:@"session.exit.reason" extra:@{ @"return_value": @(err), @"reason": @"exec_failure", @"stdio_succeeded": @NO, @"pty_exists": @NO, @"is_restart_path": @(isRestartPath) }];
        [self recordSessionAttemptEvent:@"session.attempt.fail.become_new_init_child" extra:@{ @"return_value": @(err), @"stdio_succeeded": @NO, @"pty_exists": @NO, @"is_restart_path": @(isRestartPath) }];
        return err;
    }
    trace_task_source_checkpoint("task.proof.after_become_new_init_child", current);

    char argv[4096];
    [Terminal convertCommand:command toArgs:argv limitSize:sizeof(argv)];
    const char *envp = "TERM=xterm-256color\0"
                       "HOME=/root\0"
                       "USER=root\0"
                       "LOGNAME=root\0"
                       "SHELL=/bin/sh\0"
                       "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin\0";
    NSMutableArray<NSString *> *argvStrings = [NSMutableArray array];
    const char *arg = argv;
    while (*arg != '\0') {
        [argvStrings addObject:[NSString stringWithUTF8String:arg]];
        arg += strlen(arg) + 1;
    }
    NSUInteger argc = argvStrings.count;
    char **argvp = calloc(argc + 1, sizeof(char *));
    if (argvp == NULL)
        return _ENOMEM;
    for (NSUInteger i = 0; i < argc; i++)
        argvp[i] = (char *) argvStrings[i].UTF8String;

    [ISHInstrumentation recordEvent:@"app.session.login.exec.entry"
                         attributes:@{ @"path": command.firstObject ?: @"",
                                       @"argc": @(command.count),
                                       @"mounts_non_empty": @(mounts_is_non_empty()),
                                       @"is_restart_path": @(isRestartPath) }];
    [self recordSessionAttemptEvent:@"login.exec.entry" extra:@{ @"stdio_succeeded": @YES, @"pty_exists": @YES, @"is_restart_path": @(isRestartPath) }];
    trace_task_source_checkpoint("task.proof.login.exec.entry", current);

    self.lastTaskStartEntered = YES;
    IXLandSetGuestSessionActive(YES);
    linux_start_session(command[0].UTF8String, (const char *const *) argvp, envp, ^(int retval, int pid, nsobj_t terminalObject) {
        void (^handleStartResult)(void) = ^{
            self.lastLoginExecReturnValue = retval;
            if (retval < 0) {
                IXLandSetGuestSessionActive(NO);
                self.lastPTYCreationReturnValue = retval;
                self.lastSTDIOCreateReturnValue = retval;
                [self recordSessionStartupFailureLabel:@"linux_start_session_failed"
                                           failingCall:@"linux_start_session"
                                           returnValue:retval
                                                  path:command.firstObject
                                         ptyDevicePath:nil
                                       mountsNonEmpty:mounts_is_non_empty()];
                [ISHInstrumentation recordEvent:@"app.session.failure"
                                     attributes:@{ @"failing_call": @"linux_start_session",
                                                   @"return_value": @(retval),
                                                   @"errno_style_code": @(retval),
                                                   @"path": command.firstObject ?: @"",
                                                   @"mounts_non_empty": @(mounts_is_non_empty()),
                                                   @"is_restart_path": @(isRestartPath) }];
                [self recordSessionAttemptEvent:@"login.exec.failure" extra:@{ @"return_value": @(retval), @"stdio_succeeded": @NO, @"pty_exists": @NO, @"is_restart_path": @(isRestartPath) }];
                [self recordSessionAttemptEvent:@"session.attempt.fail.linux_start_session" extra:@{ @"return_value": @(retval), @"is_restart_path": @(isRestartPath) }];
                NSString *subtitle = [self sessionFailureSubtitleForReturnValue:retval];
                [self showMessage:@"could not start session" subtitle:subtitle];
                free(argvp);
                return;
            }

            Terminal *terminal = (__bridge Terminal *) terminalObject;
            self.lastPTYCreationReturnValue = 0;
            self.lastSTDIOCreateReturnValue = 0;
            self.sessionPid = pid;
            self.sessionTerminal = terminal;
            self.sessionTerminal.attemptSequence = self.sessionAttemptSequence;
            self.sessionTerminal.sessionGeneration = self.activeSessionGeneration;
            self.sessionTerminal.guestPID = self.sessionPid;
            self.sessionTerminal.restartPath = isRestartPath;
            self.sessionTerminal.hasSessionTerminal = (self.sessionTerminal != nil);
            self.sessionTerminal.firstPTYByteSeen = NO;
            [ISHInstrumentation recordEvent:@"app.session.login.exec.exit"
                                 attributes:@{ @"return_value": @(retval),
                                               @"errno_style_code": @(retval),
                                               @"path": command.firstObject ?: @"",
                                               @"argc": @(command.count),
                                               @"mounts_non_empty": @(mounts_is_non_empty()),
                                               @"is_restart_path": @(isRestartPath) }];
            [self recordSessionAttemptEvent:@"login.exec.success" extra:@{ @"stdio_succeeded": @YES, @"pty_exists": @YES, @"is_restart_path": @(isRestartPath) }];
            ixland_guest_trace_set_context((int64_t) self.sessionAttemptSequence,
                                           (int64_t) self.sessionPid,
                                           (int64_t) getpid(),
                                           isRestartPath,
                                           self.sessionTerminal != nil);
            ixland_guest_trace_set_guest_pid((int64_t) self.sessionPid);
            [self recordSessionAttemptEvent:@"session.guest.starting" extra:@{ @"guest_pid": @(self.sessionPid), @"is_restart_path": @(isRestartPath) }];
            [ISHInstrumentation recordEvent:@"app.session.task_start.exit"
                                 attributes:@{ @"return_value": @0,
                                               @"errno_style_code": @0,
                                               @"guest_pid": @(self.sessionPid),
                                               @"path": command.firstObject ?: @"",
                                               @"mounts_non_empty": @(mounts_is_non_empty()),
                                               @"is_restart_path": @(isRestartPath) }];
            free(argvp);
        };

        if ([NSThread isMainThread]) {
            handleStartResult();
        } else {
            dispatch_async(dispatch_get_main_queue(), handleStartResult);
        }
    });

    return 0;

}

- (void)processExited:(NSNotification *)notif {
    BOOL isTesting = NSProcessInfo.processInfo.environment[@"XCTestConfigurationFilePath"] != nil;
    int pid = [notif.userInfo[@"pid"] intValue];
    int code = [notif.userInfo[@"code"] intValue];
    uint64_t observedGeneration = self.activeSessionGeneration;
    BOOL firstPTYByteSeen = self.sessionTerminal.firstPTYByteSeen;
    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    long long uptimeMs = self.activeSessionStartTime > 0 ? (long long)((now - self.activeSessionStartTime) * 1000.0) : 0;
    [self recordSessionAttemptEvent:@"session.exit.observed" extra:@{ @"notif_pid": @(pid), @"code": @(code), @"observed_generation": @(observedGeneration), @"first_pty_byte_seen": @(firstPTYByteSeen), @"uptime_ms": @(uptimeMs) }];
    // In shell-only mode, sessionPid is -1 (no live session)
    // Skip all exit handling because there's no guest to exit
    if (self.sessionPid < 0)
        return;
    if (pid != self.sessionPid)
        return;

    if (observedGeneration != self.activeSessionGeneration) {
        [self recordSessionAttemptEvent:@"session.restart.blocked.stale_generation" extra:@{ @"observed_generation": @(observedGeneration), @"active_generation": @(self.activeSessionGeneration) }];
        return;
    }
    if (self.lastHandledExitGeneration == observedGeneration) {
        [self recordSessionAttemptEvent:@"session.restart.blocked.stale_generation" extra:@{ @"observed_generation": @(observedGeneration), @"active_generation": @(self.activeSessionGeneration), @"reason": @"duplicate_exit_callback" }];
        return;
    }
    self.lastHandledExitGeneration = observedGeneration;

    NSString *exitReason = @"signal_or_interrupt_exit";
    if (code == 0) {
        exitReason = @"clean_exit";
    } else if (code < 0) {
        exitReason = @"crash";
    }
    if (!firstPTYByteSeen) {
        exitReason = @"pre_pty_failure";
    } else if (code != 0) {
        exitReason = @"post_pty_exit";
    }
    [self recordSessionAttemptEvent:@"session.exit.reason" extra:@{ @"reason": exitReason, @"code": @(code), @"first_pty_byte_seen": @(firstPTYByteSeen), @"uptime_ms": @(uptimeMs) }];

    self.lastExitedAttemptSequence = self.sessionAttemptSequence;
    [self.sessionTerminal destroy];

    BOOL allowRestart = YES;
    NSString *decisionReason = @"allowed";
    if (!firstPTYByteSeen) {
        if (isTesting) {
            decisionReason = @"pre_pty_failure_test_mode";
        } else {
            allowRestart = NO;
            decisionReason = @"pre_pty_failure";
            [self recordSessionAttemptEvent:@"session.restart.blocked.pre_pty" extra:@{ @"uptime_ms": @(uptimeMs) }];
        }
    } else if (uptimeMs < 1000) {
        allowRestart = NO;
        decisionReason = @"early_exit";
        [self recordSessionAttemptEvent:@"session.restart.blocked.early_exit" extra:@{ @"uptime_ms": @(uptimeMs), @"threshold_ms": @1000 }];
    } else if (self.postPTYRestartBudget <= 0) {
        allowRestart = NO;
        decisionReason = @"budget";
        [self recordSessionAttemptEvent:@"session.restart.blocked.budget" extra:@{ @"uptime_ms": @(uptimeMs), @"budget": @(self.postPTYRestartBudget) }];
    }

    [self recordSessionAttemptEvent:(allowRestart ? @"session.restart.allowed" : @"session.restart.suppressed") extra:@{ @"reason": decisionReason, @"uptime_ms": @(uptimeMs), @"first_pty_byte_seen": @(firstPTYByteSeen), @"budget": @(self.postPTYRestartBudget) }];

    // On iOS 13, there are multiple windows, so just close this one.
    if (@available(iOS 13, *)) {
        if (UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad && self.sceneSession != nil) {
            [UIApplication.sharedApplication requestSceneSessionDestruction:self.sceneSession options:nil errorHandler:^(NSError *error) {
                self.sceneSession = nil;
                [self processExited:notif];
            }];
            return;
        }
    }

    current = NULL; // it's been freed
    IXLandSetGuestSessionActive(NO);
    if (!allowRestart) {
        return;
    }

    if (firstPTYByteSeen && self.postPTYRestartBudget > 0) {
        self.postPTYRestartBudget -= 1;
    }
    [self recordSessionAttemptEvent:@"session.restart.requested" extra:@{ @"reason": decisionReason, @"remaining_budget": @(self.postPTYRestartBudget) }];
    [self startNewSession];
}

- (void)showMessage:(NSString *)message subtitle:(NSString *)subtitle {
    [self recordSessionAttemptEvent:@"session.attempt.showMessage.entry" extra:@{ @"message": message ?: @"", @"subtitle": subtitle ?: @"" }];
    dispatch_async(dispatch_get_main_queue(), ^{
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:message message:subtitle preferredStyle:UIAlertControllerStyleAlert];
        UIAlertAction *action = [UIAlertAction actionWithTitle:@"k"
                                                         style:UIAlertActionStyleDefault
                                                       handler:nil];
        [alert addAction:action];
        alert.view.accessibilityIdentifier = @"SessionStartupAlertView";
        [self presentViewController:alert animated:YES completion:nil];
    });
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context {
    if (object == [UserPreferences shared]) {
        [self _updateStyleFromPreferences:YES];
    } else {
        [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
    }
}

- (void)_updateStyleFromPreferences:(BOOL)animated {
    NSAssert(NSThread.isMainThread, @"This method needs to be called on the main thread");
    NSTimeInterval duration = animated ? 0.1 : 0;
    [UIView animateWithDuration:duration animations:^{
        self.view.backgroundColor = [[UIColor alloc] ish_initWithHexString:UserPreferences.shared.palette.backgroundColor];
        UIKeyboardAppearance keyAppearance = UserPreferences.shared.keyboardAppearance;
        self.termView.keyboardAppearance = keyAppearance;
        for (BarButton *button in self.barButtons) {
            button.keyAppearance = keyAppearance;
        }
        UIColor *tintColor = keyAppearance == UIKeyboardAppearanceLight ? UIColor.blackColor : UIColor.whiteColor;
        for (UIControl *control in self.barControls) {
            control.tintColor = tintColor;
        }
    }];
    UIView *oldBarView = self.termView.inputAccessoryView;
    self.termView.inputAccessoryView = self.terminal.loaded ? self.terminal.webView.inputAccessoryView : nil;
    if (self.termView.inputAccessoryView != oldBarView && self.termView.isFirstResponder) {
        dispatch_async(dispatch_get_main_queue(), ^{
            self.ignoreKeyboardMotion = YES; // avoid infinite recursion
            [self.termView reloadInputViews];
            self.ignoreKeyboardMotion = NO;
        });
    }
}
- (void)_updateStyleAnimated {
    [self _updateStyleFromPreferences:YES];
}

- (void)_updateBadge {
    self.settingsBadge.hidden = !FsNeedsRepositoryUpdate();
}

- (UIStatusBarStyle)preferredStatusBarStyle {
    return UserPreferences.shared.statusBarStyle;
}

- (BOOL)prefersStatusBarHidden {
    return UserPreferences.shared.hideStatusBar;
}

- (void)keyboardDidSomething:(NSNotification *)notification {
    (void) notification;
    self.bottomConstraint.constant = 0;
}

- (void)setHasExternalKeyboard:(BOOL)hasExternalKeyboard {
    _hasExternalKeyboard = hasExternalKeyboard;
    [self _updateStyleFromPreferences:YES];
}

- (void)prepareForSegue:(UIStoryboardSegue *)segue sender:(id)sender {
    if ([segue.identifier isEqualToString:@"embed"]) {
        // You might want to check if this is your embed segue here
        // in case there are other segues triggered from this view controller.
        segue.destinationViewController.view.translatesAutoresizingMaskIntoConstraints = NO;
    }
}

- (void)traitCollectionDidChange:(UITraitCollection *)previousTraitCollection {
    // Hack to resolve a layering mismatch between the UI and preferences.
    if (@available(iOS 12.0, *)) {
        if (previousTraitCollection.userInterfaceStyle != self.traitCollection.userInterfaceStyle) {
            // Ensure that the relevant things listening for this will update.
            UserPreferences.shared.colorScheme = UserPreferences.shared.colorScheme;
        }
    }
}

- (void)dealloc {
    // Nothing to clean up; controller does not create window-level proxies.
}

#pragma mark Bar

- (IBAction)showAbout:(id)sender {
    UINavigationController *navigationController = [[UIStoryboard storyboardWithName:@"About" bundle:nil] instantiateInitialViewController];
    if ([sender isKindOfClass:[UIGestureRecognizer class]]) {
        UIGestureRecognizer *recognizer = sender;
        if (recognizer.state == UIGestureRecognizerStateBegan) {
            AboutViewController *aboutViewController = (AboutViewController *) navigationController.topViewController;
            aboutViewController.includeDebugPanel = YES;
        } else {
            return;
        }
    }
    [self presentViewController:navigationController animated:YES completion:nil];
    [self.termView resignFirstResponder];
}

- (void)resizeBar {
    CGSize bar = self.barView.bounds.size;
    // set sizing parameters on bar
    // numbers stolen from iVim and modified somewhat
    if (UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPhone) {
        // phone
        [self setBarHorizontalPadding:6 verticalPadding:6 buttonWidth:32];
    } else if (bar.width >= 450) {
        // wide ipad
        [self setBarHorizontalPadding:15 verticalPadding:8 buttonWidth:43];
    } else {
        // narrow ipad (slide over)
        [self setBarHorizontalPadding:10 verticalPadding:8 buttonWidth:36];
    }
    [UIView performWithoutAnimation:^{
        [self.barView layoutIfNeeded];
    }];
}

- (void)setBarHorizontalPadding:(CGFloat)horizontal verticalPadding:(CGFloat)vertical buttonWidth:(CGFloat)buttonWidth {
    self.barLeading.constant = self.barTrailing.constant = horizontal;
    self.barTop.constant = self.barBottom.constant = vertical;
    self.barButtonWidth.constant = buttonWidth;
}

- (IBAction)pressEscape:(id)sender {
    [self pressKey:@"\x1b"];
}
- (IBAction)pressTab:(id)sender {
    [self pressKey:@"\t"];
}
- (void)pressKey:(NSString *)key {
    [self.termView insertText:key];
}

- (IBAction)pressControl:(id)sender {
    self.controlKey.selected = !self.controlKey.selected;
}
    
- (IBAction)pressArrow:(ArrowBarButton *)sender {
    switch (sender.direction) {
        case ArrowUp: [self pressKey:[self.terminal arrow:'A']]; break;
        case ArrowDown: [self pressKey:[self.terminal arrow:'B']]; break;
        case ArrowLeft: [self pressKey:[self.terminal arrow:'D']]; break;
        case ArrowRight: [self pressKey:[self.terminal arrow:'C']]; break;
        case ArrowNone: break;
    }
}

- (void)switchTerminal:(UIKeyCommand *)sender {
    unsigned i = (unsigned) sender.input.integerValue;
    if (i == 7)
        self.terminal = self.sessionTerminal;
    else
        self.terminal = [Terminal terminalWithType:TTY_CONSOLE_MAJOR number:i];
}

- (void)increaseFontSize:(UIKeyCommand *)command {
    self.termView.overrideFontSize = self.termView.effectiveFontSize + 1;
}
- (void)decreaseFontSize:(UIKeyCommand *)command {
    self.termView.overrideFontSize = self.termView.effectiveFontSize - 1;
}
- (void)resetFontSize:(UIKeyCommand *)command {
    self.termView.overrideFontSize = 0;
}

- (NSArray<UIKeyCommand *> *)keyCommands {
    static NSMutableArray<UIKeyCommand *> *commands = nil;
    if (commands == nil) {
        commands = [NSMutableArray new];
        for (unsigned i = 1; i <= 7; i++) {
            [commands addObject:
             [UIKeyCommand keyCommandWithInput:[NSString stringWithFormat:@"%d", i]
                                modifierFlags:UIKeyModifierCommand|UIKeyModifierAlternate|UIKeyModifierShift
                                       action:@selector(switchTerminal:)]];
        }
        UIKeyCommand *increaseFontCommand = [UIKeyCommand keyCommandWithInput:@"+"
                                                               modifierFlags:UIKeyModifierCommand
                                                                      action:@selector(increaseFontSize:)];
        increaseFontCommand.title = @"Increase Font Size";
        [commands addObject:increaseFontCommand];
        [commands addObject:
         [UIKeyCommand keyCommandWithInput:@"="
                            modifierFlags:UIKeyModifierCommand
                                   action:@selector(increaseFontSize:)]];
        UIKeyCommand *decreaseFontCommand = [UIKeyCommand keyCommandWithInput:@"-"
                                                               modifierFlags:UIKeyModifierCommand
                                                                      action:@selector(decreaseFontSize:)];
        decreaseFontCommand.title = @"Decrease Font Size";
        [commands addObject:decreaseFontCommand];
        UIKeyCommand *resetFontCommand = [UIKeyCommand keyCommandWithInput:@"0"
                                                           modifierFlags:UIKeyModifierCommand
                                                                  action:@selector(resetFontSize:)];
        resetFontCommand.title = @"Reset Font Size";
        [commands addObject:resetFontCommand];
        UIKeyCommand *settingsCommand = [UIKeyCommand keyCommandWithInput:@","
                                                          modifierFlags:UIKeyModifierCommand
                                                                 action:@selector(showAbout:)];
        settingsCommand.title = @"Settings";
        [commands addObject:settingsCommand];
    }
    return commands;
}

- (void)setTerminal:(Terminal *)terminal {
    _terminal = terminal;
    self.termView.terminal = self.terminal;
    self.termView.inputAccessoryView = self.terminal.loaded ? self.terminal.webView.inputAccessoryView : nil;
}

- (void)setSessionTerminal:(Terminal *)sessionTerminal {
    self.terminal = sessionTerminal;
    _sessionTerminal = sessionTerminal;
}

#pragma mark - Testing Support

// Test-only accessor for exposing terminal text to UI tests
- (NSString *)terminalScreenTextForTesting {
    return [self.terminal screenTextForTesting];
}

// Override accessibilityValue to expose terminal text for UI test queries
- (NSArray *)accessibilityElements {
    NSDictionary *environment = NSProcessInfo.processInfo.environment;
    if (environment[@"XCTestConfigurationFilePath"] != nil || environment[@"IXLAND_UI_TESTING"] != nil) {
        return self.termView.accessibilityElements;
    }
    // Expose TerminalSurface proxy as the sole accessibility element
    return @[self.termView.terminalAccessibilityElement];
}

- (BOOL)isAccessibilityElement {
    // Controller is NOT an accessibility element; delegate to TerminalSurface
    return NO;
}

- (BOOL)accessibilityActivate {
    if ([self isRunningUITests]) {
        return [self.termView focusForTesting];
    }
    return [self.termView becomeFirstResponder];
}

// Controller must not create window-level synthetic accessibility proxies.

@end

@interface BarView : UIInputView
@property (weak) IBOutlet TerminalViewController *terminalViewController;
@property (nonatomic) IBInspectable BOOL allowsSelfSizing;
@end
@implementation BarView
@dynamic allowsSelfSizing;

- (void)layoutSubviews {
    [self.terminalViewController resizeBar];
}

@end

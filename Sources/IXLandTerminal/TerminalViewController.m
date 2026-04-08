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
#import "NSObject+SaneKVO.h"
#import "LinuxInterop.h"
#import "Instrumentation/ISHRuntimeFlags.h"
#import <ISHInstrumentation.h>
#import <IXLandLinuxRuntime/kernel/init.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/kernel/guest_trace_context.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
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

@interface TerminalViewController () <UIGestureRecognizerDelegate>

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

- (void)viewDidLoad {
    [super viewDidLoad];
    self.postPTYRestartBudget = 1;
    
    // Ensure UI test surface is accessibility-exposed
    self.view.isAccessibilityElement = YES;
    self.view.accessibilityIdentifier = @"TerminalViewController";

    int bootError = [AppDelegate bootError];
    if (bootError < 0) {
        NSString *message = [NSString stringWithFormat:@"could not boot"];
        NSString *subtitle = [NSString stringWithFormat:@"error code %d", bootError];
        if (bootError == _EINVAL)
            subtitle = [subtitle stringByAppendingString:@"\n(try reinstalling the app, see release notes for details)"];
        [self showMessage:message subtitle:subtitle];
    }

    self.terminal = self.terminal;
    [self.termView becomeFirstResponder];

    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    [center addObserver:self
               selector:@selector(keyboardDidSomething:)
                   name:UIKeyboardWillChangeFrameNotification
                 object:nil];
    [center addObserver:self
               selector:@selector(keyboardDidSomething:)
                   name:UIKeyboardDidChangeFrameNotification
                 object:nil];
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
}

- (void)startNewSession {
    BOOL isRestartPath = (self.lastExitedAttemptSequence != 0);
    [self recordSessionAttemptEvent:@"session.start.requested" extra:@{ @"is_restart_path": @(isRestartPath), @"post_pty_restart_budget": @(self.postPTYRestartBudget) }];
    if (self.sessionStartInProgress) {
        [self recordSessionAttemptEvent:@"session.start.blocked.reentrant" extra:@{ @"is_restart_path": @(isRestartPath) }];
        return;
    }

    self.sessionStartInProgress = YES;
    self.sessionAttemptSequence += 1;
    self.sessionGenerationSequence += 1;
    self.activeSessionGeneration = self.sessionGenerationSequence;
    self.activeSessionStartTime = CFAbsoluteTimeGetCurrent();
    [self recordSessionAttemptEvent:@"session.generation.assigned" extra:@{ @"is_restart_path": @(isRestartPath), @"assigned_generation": @(self.activeSessionGeneration) }];
    [self recordSessionAttemptEvent:@"session.start.enter" extra:@{ @"is_restart_path": @(isRestartPath) }];

    int err = [self startSession];
    [self recordSessionAttemptEvent:@"session.start.exit" extra:@{ @"is_restart_path": @(isRestartPath), @"return_value": @(err) }];
    self.sessionStartInProgress = NO;

    if (err < 0) {
        [self recordSessionAttemptEvent:@"session.dialog.raise" extra:@{ @"return_value": @(err), @"message": @"could not start session", @"is_restart_path": @(isRestartPath) }];
        [self showMessage:@"could not start session"
                 subtitle:[NSString stringWithFormat:@"error code %d", err]];
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
    int err = become_new_init_child();
    if (err < 0) {
        [self recordSessionAttemptEvent:@"session.exit.reason" extra:@{ @"return_value": @(err), @"reason": @"exec_failure", @"stdio_succeeded": @NO, @"pty_exists": @NO, @"is_restart_path": @(self.lastExitedAttemptSequence != 0) }];
        [self recordSessionAttemptEvent:@"session.attempt.fail.become_new_init_child" extra:@{ @"return_value": @(err), @"stdio_succeeded": @NO, @"pty_exists": @NO, @"is_restart_path": @(self.lastExitedAttemptSequence != 0) }];
        return err;
    }
    trace_task_source_checkpoint("task.proof.after_become_new_init_child", current);
    struct tty *tty;
    self.sessionTerminal = nil;
    Terminal *terminal = [Terminal createPseudoTerminal:&tty];
    if (terminal == nil) {
        NSAssert(IS_ERR(tty), @"tty should be error");
        int ttyErr = (int) PTR_ERR(tty);
        [self recordSessionAttemptEvent:@"session.exit.reason" extra:@{ @"return_value": @(ttyErr), @"reason": @"exec_failure", @"stdio_succeeded": @NO, @"pty_exists": @NO, @"is_restart_path": @(self.lastExitedAttemptSequence != 0) }];
        [self recordSessionAttemptEvent:@"session.attempt.fail.create_pseudoterminal" extra:@{ @"return_value": @(ttyErr), @"stdio_succeeded": @NO, @"pty_exists": @NO, @"is_restart_path": @(self.lastExitedAttemptSequence != 0) }];
        return ttyErr;
    }
    self.sessionTerminal = terminal;
    self.sessionTerminal.attemptSequence = self.sessionAttemptSequence;
    self.sessionTerminal.sessionGeneration = self.activeSessionGeneration;
    self.sessionTerminal.guestPID = current ? current->pid : -1;
    self.sessionTerminal.restartPath = (self.lastExitedAttemptSequence != 0);
    self.sessionTerminal.hasSessionTerminal = (self.sessionTerminal != nil);
    self.sessionTerminal.firstPTYByteSeen = NO;
    NSString *stdioFile = [NSString stringWithFormat:@"/dev/pts/%d", tty->num];
    err = create_stdio(stdioFile.fileSystemRepresentation, TTY_PSEUDO_SLAVE_MAJOR, tty->num);
    if (err < 0) {
        [self recordSessionAttemptEvent:@"session.exit.reason" extra:@{ @"return_value": @(err), @"reason": @"exec_failure", @"stdio_succeeded": @NO, @"pty_exists": @YES, @"tty_num": @(tty->num), @"is_restart_path": @(self.lastExitedAttemptSequence != 0) }];
        [self recordSessionAttemptEvent:@"session.attempt.fail.create_stdio" extra:@{ @"return_value": @(err), @"stdio_succeeded": @NO, @"pty_exists": @YES, @"tty_num": @(tty->num), @"is_restart_path": @(self.lastExitedAttemptSequence != 0) }];
        return err;
    }
    trace_task_source_checkpoint("task.proof.after_create_stdio", current);

    char argv[4096];
    [Terminal convertCommand:command toArgs:argv limitSize:sizeof(argv)];
    const char *envp = "TERM=xterm-256color\0";
    
    // APPSIM-004 Stage 1: Trace /bin/login exec entry
    [self recordSessionAttemptEvent:@"login.exec.entry" extra:@{ @"stdio_succeeded": @YES, @"pty_exists": @YES, @"tty_num": @(tty->num), @"is_restart_path": @(self.lastExitedAttemptSequence != 0) }];
    trace_task_source_checkpoint("task.proof.login.exec.entry", current);
    
    tty_release(tty);
    err = do_execve(command[0].UTF8String, command.count, argv, envp);
    
    if (err < 0) {
        // APPSIM-004 Stage 1: Trace /bin/login exec failure
        [self recordSessionAttemptEvent:@"login.exec.failure" extra:@{ @"return_value": @(err), @"stdio_succeeded": @YES, @"pty_exists": @YES, @"is_restart_path": @(self.lastExitedAttemptSequence != 0) }];
        [self recordSessionAttemptEvent:@"session.exit.reason" extra:@{ @"return_value": @(err), @"reason": @"exec_failure", @"stdio_succeeded": @YES, @"pty_exists": @YES, @"is_restart_path": @(self.lastExitedAttemptSequence != 0) }];
        [self recordSessionAttemptEvent:@"session.attempt.fail.do_execve" extra:@{ @"return_value": @(err), @"stdio_succeeded": @YES, @"pty_exists": @YES, @"is_restart_path": @(self.lastExitedAttemptSequence != 0) }];
        trace_task_source_checkpoint("task.proof.login.exec.failure", current);
        return err;
    }
    
    // APPSIM-004 Stage 1: Trace /bin/login exec success
    [self recordSessionAttemptEvent:@"login.exec.success" extra:@{ @"stdio_succeeded": @YES, @"pty_exists": @YES, @"is_restart_path": @(self.lastExitedAttemptSequence != 0) }];
    trace_task_source_checkpoint("task.proof.login.exec.success", current);
    ixland_guest_trace_set_context((int64_t) self.sessionAttemptSequence,
                                   (int64_t) (current ? current->pid : -1),
                                   (int64_t) getpid(),
                                   self.lastExitedAttemptSequence != 0,
                                   self.sessionTerminal != nil);
    ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_EXEC, "guest.post_exec.success");
    
    // APPSIM-004 Stage 1: Verify PID remains alive after exec
    if (current != NULL && current->pid != 0) {
        [ISHInstrumentation recordEvent:@"login.pid_alive"];
        trace_task_source_checkpoint("task.proof.login.pid.alive", current);
    }
    
    trace_task_source_checkpoint("task.proof.after_do_execve", current);
    self.sessionPid = current->pid;

    // CRITICAL: Memory barrier to ensure all stores from do_execve() are visible
    // before task_start() creates the child thread. do_execve() modifies
    // current->mm and current->mem via mm_release/task_set_mm window.
    // Without this barrier, the child thread may see NULL current->mem.
    __sync_synchronize();

    // Record semantic event before starting guest thread
    [self recordSessionAttemptEvent:@"guest.thread.start" extra:nil];

    trace_task_source_checkpoint("task.proof.before_task_start_callsite", current);
    [self recordSessionAttemptEvent:@"guest.thread.before_task_start" extra:nil];
    ixland_guest_trace_set_context((int64_t) self.sessionAttemptSequence,
                                   (int64_t) (current ? current->pid : -1),
                                   (int64_t) getpid(),
                                   self.lastExitedAttemptSequence != 0,
                                   self.sessionTerminal != nil);
    ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_TASK, "guest.task_start.before_call");

    task_start(current);
    [self recordSessionAttemptEvent:@"guest.thread.after_task_start_return" extra:nil];
    ixland_guest_trace_emit(IXLAND_INSTRUMENTATION_ORIGIN_TASK, "guest.task_start.after_return");

    // task_start creates a detached thread that runs the child process
    // Return to allow normal UIKit runloop management
    return 0;
}

- (void)processExited:(NSNotification *)notif {
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
        allowRestart = NO;
        decisionReason = @"pre_pty_failure";
        [self recordSessionAttemptEvent:@"session.restart.blocked.pre_pty" extra:@{ @"uptime_ms": @(uptimeMs) }];
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
        [alert addAction:[UIAlertAction actionWithTitle:@"k"
                                                  style:UIAlertActionStyleDefault
                                                handler:nil]];
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
    if (UserPreferences.shared.hideExtraKeysWithExternalKeyboard && self.hasExternalKeyboard) {
        self.termView.inputAccessoryView = nil;
    } else {
        self.termView.inputAccessoryView = self.barView;
    }
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
    if (self.ignoreKeyboardMotion)
        return;

    CGRect screenKeyboardFrame = [notification.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
    UIScreen *screen = UIScreen.mainScreen;
    // notification.object is nil before iOS 16.1 and the correct UIScreen after iOS 16.1
    if (notification.object != nil)
        screen = notification.object;
    CGRect keyboardFrame = [self.view convertRect:screenKeyboardFrame fromCoordinateSpace:screen.coordinateSpace];
    if (CGRectEqualToRect(keyboardFrame, CGRectZero))
        return;
    CGRect intersection = CGRectIntersection(keyboardFrame, self.view.bounds);
    keyboardFrame = intersection;
    self.hasExternalKeyboard = keyboardFrame.size.height < 100;
    CGFloat pad = CGRectGetMaxY(self.view.bounds) - CGRectGetMinY(keyboardFrame);
    // The keyboard appears to be undocked. This means it can either be split or
    // truly floating. In the former case we want to keep the pad, but in the
    // latter we should fall back to the input accessory view instead of the
    // keyboard.
    if (pad != keyboardFrame.size.height && keyboardFrame.size.width != UIScreen.mainScreen.bounds.size.width) {
        pad = MAX(self.view.safeAreaInsets.bottom, self.termView.inputAccessoryView.frame.size.height);
    }
    self.bottomConstraint.constant = pad;

    BOOL initialLayout = self.termView.needsUpdateConstraints;
    [self.view setNeedsUpdateConstraints];
    if (!initialLayout) {
        // if initial layout hasn't happened yet, the terminal view is going to be at a really weird place, so animating it is going to look really bad
        NSNumber *interval = notification.userInfo[UIKeyboardAnimationDurationUserInfoKey];
        NSNumber *curve = notification.userInfo[UIKeyboardAnimationCurveUserInfoKey];
        [UIView animateWithDuration:interval.doubleValue
                              delay:0
                            options:curve.integerValue << 16
                         animations:^{
                             [self.view layoutIfNeeded];
                         }
                         completion:nil];
    }
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
}

- (void)setSessionTerminal:(Terminal *)sessionTerminal {
    if (_terminal == _sessionTerminal)
        self.terminal = sessionTerminal;
    _sessionTerminal = sessionTerminal;
}

#pragma mark - Testing Support

// Test-only accessor for exposing terminal text to UI tests
- (NSString *)terminalScreenTextForTesting {
    return [self.terminal screenTextForTesting];
}

// Override accessibilityValue to expose terminal text for UI test queries
- (NSString *)accessibilityValue {
    // Return terminal screen text if available, otherwise fall back to default
    NSString *terminalText = [self.terminal screenTextForTesting];
    if (terminalText.length > 0) {
        return terminalText;
    }
    return [super accessibilityValue];
}

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

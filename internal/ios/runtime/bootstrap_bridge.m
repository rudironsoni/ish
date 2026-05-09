#import "bootstrap_bridge.h"

#import "Sources/IXLandTerminal/AppDelegate.h"
#import "Sources/IXLandTerminal/ExceptionExfiltrator.h"
#import "Sources/IXLandTerminal/LocationDevice.h"
#import "Sources/IXLandTerminal/PasteboardDevice.h"
#import "internal/ios/fs/backing_fs.h"

#import <IXLandLinuxRuntime/fs/devices.h>
#import <IXLandLinuxRuntime/fs/dyndev.h>
#import <IXLandLinuxRuntime/fs/path.h>
#import <IXLandLinuxRuntime/fs/sock.h>
#import <IXLandLinuxRuntime/kernel/calls.h>
#import <IXLandLinuxRuntime/kernel/errno.h>
#import <IXLandLinuxRuntime/kernel/task.h>
#import <IXLandLinuxRuntime/util/debug.h>
#include <stdlib.h>

@interface AppDelegate (RuntimeBootstrapBridge)
+ (int)bootError;
+ (BOOL)lastBootstrapRootPresent;
+ (BOOL)lastBootstrapRootExists;
+ (BOOL)lastBootstrapRootDataExists;
+ (BOOL)lastBootstrapRootsAvailable;
+ (BOOL)lastBootstrapArchiveURLPresent;
+ (BOOL)lastBootstrapImportAttempted;
+ (BOOL)lastBootstrapImportSucceeded;
+ (NSString *)lastBootstrapImportErrorDescription;
+ (BOOL)lastBootstrapMountRootCalled;
+ (int)lastRootMountReturnValue;
+ (BOOL)lastMountsNonEmptyAfterRootMount;
+ (BOOL)lastBootstrapBecomeFirstProcessCalled;
+ (int)lastBecomeFirstProcessReturnValue;
+ (BOOL)lastBootstrapPID1ExistsAfterBecomeFirstProcess;
+ (int)lastBootstrapReturnValue;
+ (int)bootstrapRuntimeForSession;
@end

int runtime_bootstrap_session(void)
{
    return [AppDelegate bootstrapRuntimeForSession];
}

int runtime_boot_error(void)
{
    return [AppDelegate bootError];
}

void runtime_get_bootstrap_diagnostics(struct runtime_bootstrap_diagnostics *diagnostics)
{
    if (diagnostics == NULL)
        return;

    diagnostics->root_present = [AppDelegate lastBootstrapRootPresent];
    diagnostics->root_exists = [AppDelegate lastBootstrapRootExists];
    diagnostics->root_data_exists = [AppDelegate lastBootstrapRootDataExists];
    diagnostics->roots_available = [AppDelegate lastBootstrapRootsAvailable];
    diagnostics->archive_url_present = [AppDelegate lastBootstrapArchiveURLPresent];
    diagnostics->import_attempted = [AppDelegate lastBootstrapImportAttempted];
    diagnostics->import_succeeded = [AppDelegate lastBootstrapImportSucceeded];
    diagnostics->root_mount_called = [AppDelegate lastBootstrapMountRootCalled];
    diagnostics->mounts_non_empty_after_root_mount = [AppDelegate lastMountsNonEmptyAfterRootMount];
    diagnostics->become_first_process_called = [AppDelegate lastBootstrapBecomeFirstProcessCalled];
    diagnostics->pid1_exists_after_become_first_process = [AppDelegate lastBootstrapPID1ExistsAfterBecomeFirstProcess];
    diagnostics->root_mount_return_value = [AppDelegate lastRootMountReturnValue];
    diagnostics->become_first_process_return_value = [AppDelegate lastBecomeFirstProcessReturnValue];
    diagnostics->bootstrap_return_value = [AppDelegate lastBootstrapReturnValue];
    diagnostics->import_error_description = [[AppDelegate lastBootstrapImportErrorDescription] UTF8String];
}

static void runtime_handle_exit(struct task *task, int code)
{
    if (task->parent != NULL && task->parent->parent != NULL)
        return;

    pid_t pid = task->pid;
    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSNotificationCenter defaultCenter] postNotificationName:ProcessExitedNotification
                                                            object:nil
                                                          userInfo:@{@"pid": @(pid),
                                                                     @"code": @(code)}];
    });
}

static void runtime_handle_die(const char *msg)
{
    NSString *message = [NSString stringWithFormat:@"%s: %s", __func__, msg];
    iSHExceptionHandler([[NSException alloc] initWithName:NSGenericException reason:message userInfo:nil]);
}

int runtime_finish_post_mount_setup(void)
{
    int err = dyn_dev_register(&clipboard_dev, DEV_CHAR, DYN_DEV_MAJOR, DEV_CLIPBOARD_MINOR);
    if (err != 0)
        return err;
    generic_mknodat(AT_PWD, "/dev/clipboard", S_IFCHR | 0666, dev_make(DYN_DEV_MAJOR, DEV_CLIPBOARD_MINOR));

    err = dyn_dev_register(&location_dev, DEV_CHAR, DYN_DEV_MAJOR, DEV_LOCATION_MINOR);
    if (err != 0)
        return err;
    generic_mknodat(AT_PWD, "/dev/location", S_IFCHR | 0666, dev_make(DYN_DEV_MAJOR, DEV_LOCATION_MINOR));

    iosfs_init();
    return 0;
}

void runtime_install_process_hooks(void)
{
    exit_hook = runtime_handle_exit;
    die_handler = runtime_handle_die;
}

void runtime_configure_socket_prefix(void)
{
#if !TARGET_OS_SIMULATOR
    if (sock_tmp_prefix == NULL) {
        NSString *sockTmp = [NSTemporaryDirectory() stringByAppendingString:@"ishsock"];
        sock_tmp_prefix = strdup(sockTmp.UTF8String);
    }
#endif
}

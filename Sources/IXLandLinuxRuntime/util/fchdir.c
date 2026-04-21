#import <IXLandLinuxRuntime/util/sync.h>
#include <unistd.h>

static lock_t fchdir_lock = LOCK_INITIALIZER;

void lock_fchdir(int dirfd)
{
    lock(&fchdir_lock);
    fchdir(dirfd);
}

void unlock_fchdir(void)
{
    unlock(&fchdir_lock);
}
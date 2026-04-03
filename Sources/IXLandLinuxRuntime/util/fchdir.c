#include <unistd.h>
#import <IXLandLinuxRuntime/util/sync.h>

static lock_t fchdir_lock = LOCK_INITIALIZER;

void lock_fchdir(int dirfd) {
    lock(&fchdir_lock);
    fchdir(dirfd);
}

void unlock_fchdir() {
    unlock(&fchdir_lock);
}
#include "root_bootstrap.h"

#include <IXLandLinuxRuntime/kernel/fs.h>
#include <IXLandLinuxRuntime/kernel/init.h>

int ios_rootfs_mount(const char *root_path) {
    return mount_root(&rootfs, root_path);
}

#ifndef IXLAND_BACKING_FS_H
#define IXLAND_BACKING_FS_H

#include <IXLandLinuxRuntime/kernel/fs.h>

extern const struct fs_ops iosfs;
extern const struct fs_ops iosfs_unsafe;

void iosfs_init(void);
void iosfs_clear_all_bookmarks(void);

#endif

#ifndef ISH_INTERNAL
#error "for internal use only"
#endif

#ifndef FS_FAKE_H
#define FS_FAKE_H

#import <IXLandLinuxRuntime/kernel/fs.h>
#import <IXLandLinuxRuntime/fs/fake-db.h>
#import <IXLandLinuxRuntime/util/misc.h>

struct fd *fakefs_open_inode(struct mount *mount, ino_t inode);

#endif

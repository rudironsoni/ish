#import "root_archive.h"

#import "rootfs_metadata.h"

#import <archive.h>
#import <archive_entry.h>
#import <IXLandLinuxRuntime/fs/dev.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

@protocol IXLandRootArchiveProgressReporting

- (void)updateProgress:(double)progressFraction message:(NSString *)progressMessage;
- (BOOL)shouldCancel;

@end

static void report_progress(id reporter, double progress, NSString *message) {
    if (reporter != nil && [reporter respondsToSelector:@selector(updateProgress:message:)]) {
        [(id<IXLandRootArchiveProgressReporting>) reporter updateProgress:progress message:message];
    }
}

static BOOL should_cancel(id reporter) {
    return reporter != nil && [reporter respondsToSelector:@selector(shouldCancel)] &&
           [(id<IXLandRootArchiveProgressReporting>) reporter shouldCancel];
}

static NSError *root_archive_error(NSInteger code, NSString *description) {
    return [NSError errorWithDomain:NSPOSIXErrorDomain
                               code:code
                           userInfo:@{NSLocalizedDescriptionKey: description}];
}

static NSString *root_archive_relative_path(NSURL *rootURL, NSURL *url) {
    NSString *rootPath = rootURL.path;
    NSString *path = url.path;
    if (![path hasPrefix:rootPath]) {
        return nil;
    }
    NSString *relative = [path substringFromIndex:rootPath.length];
    if ([relative hasPrefix:@"/"]) {
        relative = [relative substringFromIndex:1];
    }
    return relative;
}

static BOOL root_archive_is_safe_relative_path(NSString *relativePath) {
    if (relativePath.length == 0 || [relativePath hasPrefix:@"/"]) {
        return NO;
    }
    for (NSString *component in relativePath.pathComponents) {
        if ([component isEqualToString:@".."]) {
            return NO;
        }
    }
    return YES;
}

static BOOL root_archive_set_linux_stat(NSString *fullPath,
                                        mode_t mode,
                                        uid_t uid,
                                        gid_t gid,
                                        dev_t rdev,
                                        BOOL nofollow) {
    struct rootfs_stat linux_stat = {
        .mode = (uint32_t) mode,
        .uid = (uint32_t) uid,
        .gid = (uint32_t) gid,
        .rdev = (uint32_t) rdev,
    };
    return rootfs_write_full_path_stat(fullPath.fileSystemRepresentation, &linux_stat, nofollow);
}

static int root_archive_copy_data(struct archive *reader, int fd) {
    const void *buffer = NULL;
    size_t size = 0;
    la_int64_t offset = 0;
    int status = ARCHIVE_OK;
    while ((status = archive_read_data_block(reader, &buffer, &size, &offset)) == ARCHIVE_OK) {
        if (pwrite(fd, buffer, size, offset) != (ssize_t) size) {
            return errno;
        }
    }
    if (status == ARCHIVE_EOF) {
        return 0;
    }
    return archive_errno(reader) ?: EIO;
}

static BOOL root_archive_import_entry(struct archive *reader,
                                      struct archive_entry *entry,
                                      NSURL *destination,
                                      id reporter,
                                      NSError **error) {
    const char *pathname = archive_entry_pathname(entry);
    if (pathname == NULL) {
        if (error != NULL) {
            *error = root_archive_error(EINVAL, @"archive entry missing path");
        }
        return NO;
    }

    NSString *relativePath = [NSString stringWithUTF8String:pathname];
    if (!root_archive_is_safe_relative_path(relativePath)) {
        if (error != NULL) {
            *error = root_archive_error(EINVAL, [NSString stringWithFormat:@"unsafe archive path: %@", relativePath]);
        }
        return NO;
    }

    NSURL *outputURL = [destination URLByAppendingPathComponent:relativePath];
    NSURL *parentURL = [outputURL URLByDeletingLastPathComponent];
    if (parentURL != nil) {
        NSError *directoryError = nil;
        if (![NSFileManager.defaultManager createDirectoryAtURL:parentURL
                                    withIntermediateDirectories:YES
                                                     attributes:@{}
                                                          error:&directoryError]) {
            if (error != NULL) {
                *error = directoryError;
            }
            return NO;
        }
    }

    mode_t mode = archive_entry_mode(entry);
    uid_t uid = (uid_t) archive_entry_uid(entry);
    gid_t gid = (gid_t) archive_entry_gid(entry);
    dev_t rdev = makedev((int) archive_entry_rdevmajor(entry), (int) archive_entry_rdevminor(entry));
    const mode_t fileType = mode & S_IFMT;

    if (should_cancel(reporter)) {
        if (error != NULL) {
            *error = nil;
        }
        return NO;
    }

    int fd = -1;
    switch (fileType) {
    case S_IFDIR:
        if (![NSFileManager.defaultManager createDirectoryAtURL:outputURL
                                    withIntermediateDirectories:YES
                                                     attributes:@{}
                                                          error:error]) {
            return NO;
        }
        if (!root_archive_set_linux_stat(outputURL.path, mode, uid, gid, rdev, NO)) {
            if (error != NULL) {
                *error = root_archive_error(errno, [NSString stringWithFormat:@"failed to store Linux metadata for %@", relativePath]);
            }
            return NO;
        }
        return YES;

    case S_IFLNK: {
        const char *target = archive_entry_symlink(entry);
        if (target == NULL || symlink(target, outputURL.fileSystemRepresentation) != 0) {
            if (error != NULL) {
                *error = root_archive_error(errno ?: EINVAL, [NSString stringWithFormat:@"failed to create symlink %@", relativePath]);
            }
            return NO;
        }
        root_archive_set_linux_stat(outputURL.path, mode, uid, gid, rdev, YES);
        return YES;
    }

    case S_IFIFO:
        if (mkfifo(outputURL.fileSystemRepresentation, 0600) != 0) {
            if (error != NULL) {
                *error = root_archive_error(errno, [NSString stringWithFormat:@"failed to create fifo %@", relativePath]);
            }
            return NO;
        }
        if (!root_archive_set_linux_stat(outputURL.path, mode, uid, gid, rdev, NO)) {
            if (error != NULL) {
                *error = root_archive_error(errno, [NSString stringWithFormat:@"failed to store Linux metadata for %@", relativePath]);
            }
            return NO;
        }
        return YES;

    case S_IFCHR:
    case S_IFBLK:
    case S_IFSOCK:
        fd = open(outputURL.fileSystemRepresentation, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0) {
            if (error != NULL) {
                *error = root_archive_error(errno, [NSString stringWithFormat:@"failed to create placeholder %@", relativePath]);
            }
            return NO;
        }
        close(fd);
        if (!root_archive_set_linux_stat(outputURL.path, mode, uid, gid, rdev, NO)) {
            if (error != NULL) {
                *error = root_archive_error(errno, [NSString stringWithFormat:@"failed to store Linux metadata for %@", relativePath]);
            }
            return NO;
        }
        return YES;

    default:
        fd = open(outputURL.fileSystemRepresentation, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0) {
            if (error != NULL) {
                *error = root_archive_error(errno, [NSString stringWithFormat:@"failed to create file %@", relativePath]);
            }
            return NO;
        }
        int copyErr = root_archive_copy_data(reader, fd);
        close(fd);
        if (copyErr != 0) {
            if (error != NULL) {
                *error = root_archive_error(copyErr, [NSString stringWithFormat:@"failed to extract file %@", relativePath]);
            }
            return NO;
        }
        if (!root_archive_set_linux_stat(outputURL.path, mode, uid, gid, rdev, NO)) {
            if (error != NULL) {
                *error = root_archive_error(errno, [NSString stringWithFormat:@"failed to store Linux metadata for %@", relativePath]);
            }
            return NO;
        }
        return YES;
    }
}

BOOL ios_root_archive_import(NSURL *archive,
                             NSURL *destination,
                             id progressReporter,
                             NSError **error) {
    if (archive == nil) {
        if (error != NULL) {
            *error = root_archive_error(ENOENT, @"root archive missing from app bundle");
        }
        return NO;
    }

    NSError *directoryError = nil;
    if (![NSFileManager.defaultManager createDirectoryAtURL:destination
                                withIntermediateDirectories:YES
                                                 attributes:@{}
                                                      error:&directoryError]) {
        if (error != NULL) {
            *error = directoryError;
        }
        return NO;
    }

    report_progress(progressReporter, 0.0, @"Importing root archive");

    struct archive *reader = archive_read_new();
    archive_read_support_filter_all(reader);
    archive_read_support_format_tar(reader);
    archive_read_support_format_gnutar(reader);

    if (archive_read_open_filename(reader, archive.fileSystemRepresentation, 10240) != ARCHIVE_OK) {
        if (error != NULL) {
            *error = root_archive_error(EIO, [NSString stringWithUTF8String:archive_error_string(reader)]);
        }
        archive_read_free(reader);
        return NO;
    }

    struct archive_entry *entry = NULL;
    int status = ARCHIVE_OK;
    while ((status = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
        if (!root_archive_import_entry(reader, entry, destination, progressReporter, error)) {
            archive_read_free(reader);
            return NO;
        }
    }

    if (status != ARCHIVE_EOF) {
        if (error != NULL) {
            *error = root_archive_error(EIO, [NSString stringWithUTF8String:archive_error_string(reader)]);
        }
        archive_read_free(reader);
        return NO;
    }

    archive_read_free(reader);
    report_progress(progressReporter, 1.0, @"Imported root archive");
    return YES;
}

static BOOL root_archive_write_entry(struct archive *writer,
                                     NSURL *sourceRoot,
                                     NSURL *url,
                                     id reporter,
                                     NSError **error) {
    if (should_cancel(reporter)) {
        if (error != NULL) {
            *error = nil;
        }
        return NO;
    }

    NSString *relativePath = root_archive_relative_path(sourceRoot, url);
    if (relativePath == nil || relativePath.length == 0) {
        return YES;
    }

    struct stat host_stat;
    if (lstat(url.fileSystemRepresentation, &host_stat) != 0) {
        if (error != NULL) {
            *error = root_archive_error(errno, [NSString stringWithFormat:@"failed to stat %@", relativePath]);
        }
        return NO;
    }

    struct rootfs_stat linux_stat = {
        .mode = (uint32_t) host_stat.st_mode,
        .uid = (uint32_t) host_stat.st_uid,
        .gid = (uint32_t) host_stat.st_gid,
        .rdev = (uint32_t) dev_fake_from_real(host_stat.st_rdev),
    };
    rootfs_read_full_path_stat(url.fileSystemRepresentation, &linux_stat, true);

    struct archive_entry *entry = archive_entry_new();
    archive_entry_set_pathname(entry, relativePath.fileSystemRepresentation);
    archive_entry_set_mode(entry, linux_stat.mode);
    archive_entry_set_uid(entry, linux_stat.uid);
    archive_entry_set_gid(entry, linux_stat.gid);
    archive_entry_set_mtime(entry, host_stat.st_mtime, 0);

    int fd = -1;
    if (S_ISREG(linux_stat.mode)) {
        archive_entry_set_size(entry, host_stat.st_size);
    } else {
        archive_entry_set_size(entry, 0);
    }

    if (S_ISLNK(linux_stat.mode)) {
        char target[MAX_PATH + 1];
        ssize_t size = readlink(url.fileSystemRepresentation, target, sizeof(target) - 1);
        if (size < 0) {
            archive_entry_free(entry);
            if (error != NULL) {
                *error = root_archive_error(errno, [NSString stringWithFormat:@"failed to read symlink %@", relativePath]);
            }
            return NO;
        }
        target[size] = '\0';
        archive_entry_set_symlink(entry, target);
    } else if (S_ISCHR(linux_stat.mode) || S_ISBLK(linux_stat.mode)) {
        archive_entry_set_rdevmajor(entry, dev_major(linux_stat.rdev));
        archive_entry_set_rdevminor(entry, dev_minor(linux_stat.rdev));
    }

    if (archive_write_header(writer, entry) != ARCHIVE_OK) {
        if (error != NULL) {
            *error = root_archive_error(EIO, [NSString stringWithUTF8String:archive_error_string(writer)]);
        }
        archive_entry_free(entry);
        return NO;
    }

    if (S_ISREG(linux_stat.mode)) {
        fd = open(url.fileSystemRepresentation, O_RDONLY);
        if (fd < 0) {
            archive_entry_free(entry);
            if (error != NULL) {
                *error = root_archive_error(errno, [NSString stringWithFormat:@"failed to open %@", relativePath]);
            }
            return NO;
        }
        char buffer[16384];
        ssize_t bytesRead = 0;
        while ((bytesRead = read(fd, buffer, sizeof(buffer))) > 0) {
            if (archive_write_data(writer, buffer, bytesRead) != bytesRead) {
                close(fd);
                archive_entry_free(entry);
                if (error != NULL) {
                    *error = root_archive_error(EIO, [NSString stringWithUTF8String:archive_error_string(writer)]);
                }
                return NO;
            }
        }
        if (bytesRead < 0) {
            close(fd);
            archive_entry_free(entry);
            if (error != NULL) {
                *error = root_archive_error(errno, [NSString stringWithFormat:@"failed to read %@", relativePath]);
            }
            return NO;
        }
        close(fd);
    }

    archive_entry_free(entry);
    return YES;
}

BOOL ios_root_archive_export(NSURL *sourceRoot,
                             NSURL *archive,
                             id progressReporter,
                             NSError **error) {
    report_progress(progressReporter, 0.0, @"Exporting root archive");

    struct archive *writer = archive_write_new();
    archive_write_add_filter_gzip(writer);
    archive_write_set_format_pax_restricted(writer);
    if (archive_write_open_filename(writer, archive.fileSystemRepresentation) != ARCHIVE_OK) {
        if (error != NULL) {
            *error = root_archive_error(EIO, [NSString stringWithUTF8String:archive_error_string(writer)]);
        }
        archive_write_free(writer);
        return NO;
    }

    __block NSError *enumerationFailure = nil;
    NSDirectoryEnumerator<NSURL *> *enumerator =
        [NSFileManager.defaultManager enumeratorAtURL:sourceRoot
                           includingPropertiesForKeys:nil
                                              options:0
                                         errorHandler:^BOOL(NSURL *url, NSError *enumerationError) {
                                             enumerationFailure = enumerationError;
                                             return NO;
                                         }];
    for (NSURL *url in enumerator) {
        if (!root_archive_write_entry(writer, sourceRoot, url, progressReporter, error)) {
            archive_write_free(writer);
            return NO;
        }
    }
    if (enumerationFailure != nil) {
        if (error != NULL) {
            *error = enumerationFailure;
        }
        archive_write_free(writer);
        return NO;
    }

    archive_write_close(writer);
    archive_write_free(writer);
    report_progress(progressReporter, 1.0, @"Exported root archive");
    return YES;
}

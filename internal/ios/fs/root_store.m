#import "root_store.h"

#import "AppGroup.h"
#import "root_archive.h"

static NSString *kDefaultRoot = @"Default Root";

NSURL *ios_roots_production_directory(void) {
    static NSURL *rootsDirectory;
    static dispatch_once_t token;
    dispatch_once(&token, ^{
        NSURL *containerURL = ContainerURL();
        if (containerURL == nil) {
            NSURL *cachesDirectory = [NSFileManager.defaultManager URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask].firstObject;
            rootsDirectory = [cachesDirectory URLByAppendingPathComponent:@"IXLandTestRoots"];
        } else {
            rootsDirectory = [containerURL URLByAppendingPathComponent:@"roots"];
        }

        NSError *error = nil;
        [NSFileManager.defaultManager createDirectoryAtURL:rootsDirectory
                               withIntermediateDirectories:YES
                                                attributes:@{}
                                                     error:&error];
        if (error != nil) {
            rootsDirectory = nil;
        }
    });
    return rootsDirectory;
}

NSArray<NSString *> *ios_root_store_list(NSURL *rootsDirectory, NSError **error) {
    NSArray<NSString *> *rootNames = [NSFileManager.defaultManager contentsOfDirectoryAtPath:rootsDirectory.path error:error];
    if (rootNames == nil) {
        return nil;
    }
    for (NSString *rootName in rootNames) {
        NSURL *rootURL = ios_root_store_url(rootsDirectory, rootName);
        if (!ios_root_store_migrate_legacy_layout(rootURL, error)) {
            return nil;
        }
    }
    return rootNames;
}

NSURL *ios_root_store_url(NSURL *rootsDirectory, NSString *name) {
    return [rootsDirectory URLByAppendingPathComponent:name];
}

BOOL ios_root_store_migrate_legacy_layout(NSURL *rootURL, NSError **error) {
    NSURL *legacyDataURL = [rootURL URLByAppendingPathComponent:@"data"];
    NSURL *legacyMetaURL = [rootURL URLByAppendingPathComponent:@"meta.db"];
    NSURL *binURL = [rootURL URLByAppendingPathComponent:@"bin"];

    BOOL hasLegacyData = [[NSFileManager defaultManager] fileExistsAtPath:legacyDataURL.path];
    BOOL hasLegacyMeta = [[NSFileManager defaultManager] fileExistsAtPath:legacyMetaURL.path];
    BOOL alreadyFlattened = [[NSFileManager defaultManager] fileExistsAtPath:binURL.path];
    if (!hasLegacyData || !hasLegacyMeta || alreadyFlattened) {
        return YES;
    }

    NSArray<NSURL *> *children = [NSFileManager.defaultManager contentsOfDirectoryAtURL:legacyDataURL
                                                              includingPropertiesForKeys:nil
                                                                                 options:0
                                                                                   error:error];
    if (children == nil) {
        return NO;
    }
    for (NSURL *childURL in children) {
        NSURL *destinationURL = [rootURL URLByAppendingPathComponent:childURL.lastPathComponent];
        if (![NSFileManager.defaultManager moveItemAtURL:childURL toURL:destinationURL error:error]) {
            return NO;
        }
    }

    if (![NSFileManager.defaultManager removeItemAtURL:legacyDataURL error:error]) {
        return NO;
    }

    NSArray<NSString *> *legacyMetadataNames = @[ @"meta.db", @"meta.db-shm", @"meta.db-wal" ];
    for (NSString *metadataName in legacyMetadataNames) {
        NSURL *metadataURL = [rootURL URLByAppendingPathComponent:metadataName];
        if ([[NSFileManager defaultManager] fileExistsAtPath:metadataURL.path]) {
            if (![NSFileManager.defaultManager removeItemAtURL:metadataURL error:error]) {
                return NO;
            }
        }
    }
    return YES;
}

NSString *ios_root_store_default_root_name(void) {
    return [NSUserDefaults.standardUserDefaults stringForKey:kDefaultRoot];
}

void ios_root_store_set_default_root_name(NSString *defaultRoot) {
    [NSUserDefaults.standardUserDefaults setObject:defaultRoot forKey:kDefaultRoot];
}

NSURL *ios_root_default_archive_url(void) {
    NSURL *archiveURL = [NSBundle.mainBundle URLForResource:@"root" withExtension:@"tar.gz"];
    if (archiveURL != nil) {
        return archiveURL;
    }

    NSURL *bundleArchiveURL = [NSBundle.mainBundle.bundleURL URLByAppendingPathComponent:@"root.tar.gz"];
    if ([[NSFileManager defaultManager] fileExistsAtPath:bundleArchiveURL.path]) {
        return bundleArchiveURL;
    }
    return nil;
}

BOOL ios_root_store_import_archive(NSURL *rootsDirectory,
                                   NSURL *archive,
                                   NSString *name,
                                   id progressReporter,
                                   NSError **error) {
    if (archive == nil) {
        if (error != NULL) {
            *error = [NSError errorWithDomain:NSPOSIXErrorDomain
                                         code:ENOENT
                                     userInfo:@{NSLocalizedDescriptionKey: @"root archive missing from app bundle"}];
        }
        return NO;
    }

    NSURL *destination = ios_root_store_url(rootsDirectory, name);
    NSURL *temporaryDestination = [NSFileManager.defaultManager.temporaryDirectory
                                   URLByAppendingPathComponent:[NSProcessInfo.processInfo globallyUniqueString]];
    if (temporaryDestination == nil) {
        return NO;
    }
    if (!ios_root_archive_import(archive, temporaryDestination, progressReporter, error)) {
        [NSFileManager.defaultManager removeItemAtURL:temporaryDestination error:nil];
        return NO;
    }
    if (![NSFileManager.defaultManager moveItemAtURL:temporaryDestination toURL:destination error:error]) {
        return NO;
    }
    return YES;
}

BOOL ios_root_store_export_archive(NSURL *rootsDirectory,
                                   NSString *name,
                                   NSURL *archive,
                                   id progressReporter,
                                   NSError **error) {
    return ios_root_archive_export(ios_root_store_url(rootsDirectory, name), archive, progressReporter, error);
}

BOOL ios_root_store_destroy(NSURL *rootsDirectory, NSString *name, NSError **error) {
    return [NSFileManager.defaultManager removeItemAtURL:ios_root_store_url(rootsDirectory, name) error:error];
}

BOOL ios_root_store_rename(NSURL *rootsDirectory, NSString *name, NSString *newName, NSError **error) {
    return [NSFileManager.defaultManager moveItemAtURL:ios_root_store_url(rootsDirectory, name)
                                                 toURL:ios_root_store_url(rootsDirectory, newName)
                                                 error:error];
}

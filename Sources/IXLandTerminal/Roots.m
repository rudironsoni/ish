//
//  Roots.m
//  iSH
//
//  Created by Theodore Dubois on 6/7/20.
//

#import <UIKit/UIKit.h>
#import <FileProvider/FileProvider.h>
#import "Roots.h"
#import "AppGroup.h"
#import "NSObject+SaneKVO.h"
#import "fakefs.h"

static NSString *kDefaultRoot = @"Default Root";

@interface Roots ()
@property NSMutableOrderedSet<NSString *> *roots;
@property (strong, nonatomic) NSURL *rootsDirectory;
@property BOOL updatingDomains;
@property BOOL domainsNeedUpdate;
@property BOOL wantsVersionFile;
@property BOOL lastImportAttempted;
@property BOOL lastImportSucceeded;
@property (strong, nonatomic, nullable) NSString *lastImportErrorDescription;
@property BOOL lastArchiveURLPresent;
@end

@implementation Roots

// Production path: App Group-backed roots directory
+ (NSURL *)productionRootsDir {
    static NSURL *rootsDir;
    static dispatch_once_t token;
    dispatch_once(&token, ^{
        NSURL *containerURL = ContainerURL();
        if (containerURL == nil) {
            NSURL *cachesDir = [NSFileManager.defaultManager URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask].firstObject;
            rootsDir = [cachesDir URLByAppendingPathComponent:@"IXLandTestRoots"];
            NSError *err = nil;
            [[NSFileManager defaultManager] createDirectoryAtURL:rootsDir withIntermediateDirectories:YES attributes:@{} error:&err];
            if (err) {
                rootsDir = nil;
            }
            return;
        }
        rootsDir = [containerURL URLByAppendingPathComponent:@"roots"];
        NSFileManager *manager = [NSFileManager defaultManager];
        [manager createDirectoryAtURL:rootsDir
          withIntermediateDirectories:YES
                           attributes:@{}
                                error:nil];
    });
    return rootsDir;
}

// Factory method with production path (existing behavior)
+ (instancetype)instance {
    static Roots *roots;
    static dispatch_once_t token;
    dispatch_once(&token, ^{
        roots = [[Roots alloc] initWithRootsDirectory:[self productionRootsDir]];
    });
    return roots;
}

// Factory method with injected roots directory (test path)
+ (instancetype)instanceWithRootsDirectory:(NSURL *)rootsDirectory {
    return [[Roots alloc] initWithRootsDirectory:rootsDirectory];
}

- (instancetype)initWithRootsDirectory:(NSURL *)rootsDirectory {
    if (self = [super init]) {
        self.rootsDirectory = rootsDirectory;
        
        if (rootsDirectory == nil) {
            // No roots directory available - cannot provision rootfs
            // This is an error state, not a valid early-return
            self.roots = [NSMutableOrderedSet orderedSet];
            return self;
        }
        
        self.lastImportAttempted = NO;
        self.lastImportSucceeded = NO;
        self.lastImportErrorDescription = nil;
        self.lastArchiveURLPresent = NO;

        NSError *error = nil;
        NSArray<NSString *> *rootNames = [NSFileManager.defaultManager contentsOfDirectoryAtPath:rootsDirectory.path error:&error];
        NSAssert(error == nil, @"couldn't list roots: %@", error);
        self.roots = [rootNames mutableCopy];

        if (!self.roots.count) {
            NSError *importError;
            NSURL *archiveURL = [NSBundle.mainBundle URLForResource:@"root" withExtension:@"tar.gz"];
            if (archiveURL == nil) {
                NSURL *bundleArchiveURL = [NSBundle.mainBundle.bundleURL URLByAppendingPathComponent:@"root.tar.gz"];
                if ([[NSFileManager defaultManager] fileExistsAtPath:bundleArchiveURL.path]) {
                    archiveURL = bundleArchiveURL;
                }
            }
            self.lastArchiveURLPresent = archiveURL != nil;
            self.lastImportAttempted = YES;

            if (![self importRootFromArchive:archiveURL
                                        name:@"default"
                                       error:&importError
                            progressReporter:nil]) {
                self.lastImportSucceeded = NO;
                self.lastImportErrorDescription = importError.localizedDescription;
                self.roots = [NSMutableOrderedSet orderedSet];
                return self;
            }
            self.lastImportSucceeded = YES;
            self.lastImportErrorDescription = nil;
            _wantsVersionFile = YES;
        }
        [self observe:@[@"roots"] options:0 owner:self usingBlock:^(typeof(self) self) {
            if (self.defaultRoot == nil && self.roots.count)
                self.defaultRoot = self.roots[0];
            [self syncFileProviderDomains];
        }];
        [self syncFileProviderDomains];

        if ((!self.defaultRoot || ![self.roots containsObject:self.defaultRoot]) && self.roots.count)
            self.defaultRoot = self.roots.firstObject;
    }
    return self;
}

- (NSString *)defaultRoot {
    return [NSUserDefaults.standardUserDefaults stringForKey:kDefaultRoot];
}

void root_progress_callback(void *cookie, double progress, const char *message, bool *should_cancel) {
    id<ProgressReporter> reporter = (__bridge id<ProgressReporter>) cookie;
    [reporter updateProgress:progress message:[NSString stringWithUTF8String:message]];
}
- (void)setDefaultRoot:(NSString *)defaultRoot {
    [NSUserDefaults.standardUserDefaults setObject:defaultRoot forKey:kDefaultRoot];
}

- (NSURL *)rootUrl:(NSString *)name {
    return [self.rootsDirectory URLByAppendingPathComponent:name];
}

- (void)syncFileProviderDomains {
    if (NSProcessInfo.processInfo.environment[@"XCTestConfigurationFilePath"] != nil) {
        return;
    }

    static BOOL fileProviderAvailable = NO;
    static BOOL checked = NO;
    
    if (!checked) {
        checked = YES;
        @try {
            NSURL *testURL = [NSFileProviderManager defaultManager].documentStorageURL;
            fileProviderAvailable = (testURL != nil);
        }
        @catch (__unused NSException *exception) {
            fileProviderAvailable = NO;
        }
    }
    
    if (!fileProviderAvailable) {
        return;
    }
    
    if (self.updatingDomains) {
        self.domainsNeedUpdate = YES;
        return;
    }
    self.updatingDomains = YES;
    self.domainsNeedUpdate = NO;

    [NSFileProviderManager getDomainsWithCompletionHandler:^(NSArray<NSFileProviderDomain *> *domains, NSError *error) {
        void (^onError)(NSError *error) = ^(NSError *error) {
            if (error != nil)
                NSLog(@"error adjusting domains: %@", error);
        };
        onError(error);
        NSMutableOrderedSet<NSString *> *missingRoots = [self.roots mutableCopy];
        for (NSFileProviderDomain *domain in domains) {
            [missingRoots removeObject:domain.identifier];
        }
        for (NSString *root in missingRoots) {
            if (@available(iOS 16.0, *)) {
                NSFileProviderDomain *domain = [[NSFileProviderDomain alloc] initWithIdentifier:root displayName:root];
                [NSFileProviderManager addDomain:domain completionHandler:onError];
            } else {
                // Fallback for older iOS versions
            }
        }
        for (NSFileProviderDomain *domain in domains) {
            if (![self.roots containsObject:domain.identifier]) {
                [NSFileProviderManager removeDomain:domain completionHandler:onError];
            }
        }
        self.updatingDomains = NO;
        if (self.domainsNeedUpdate) {
            self.domainsNeedUpdate = NO;
            [self syncFileProviderDomains];
        }
    }];
}

- (BOOL)importRootFromArchive:(NSURL *)archive name:(NSString *)name error:(NSError **)error progressReporter:(id<ProgressReporter> _Nullable)progress {
    NSAssert(![self.roots containsObject:name], @"root already exists: %@", name);
    if (archive == nil) {
        if (error != NULL) {
            *error = [NSError errorWithDomain:NSPOSIXErrorDomain
                                         code:ENOENT
                                     userInfo:@{NSLocalizedDescriptionKey: @"root archive missing from app bundle"}];
        }
        return NO;
    }
    struct fakefsify_error fs_err;
    NSURL *destination = [self rootUrl:name];
    NSURL *tempDestination = [NSFileManager.defaultManager.temporaryDirectory
                              URLByAppendingPathComponent:[NSProcessInfo.processInfo globallyUniqueString]];
    if (tempDestination == nil)
        return NO;
    if (!fakefs_import(archive.fileSystemRepresentation,
                       tempDestination.fileSystemRepresentation,
                       &fs_err, (struct progress) {(__bridge void *) progress, root_progress_callback})) {
        NSString *domain = NSPOSIXErrorDomain;
        if (fs_err.type == ERR_SQLITE)
            domain = @"SQLite";
        *error = [NSError errorWithDomain:domain
                                     code:fs_err.code
                                 userInfo:@{NSLocalizedDescriptionKey:
                                                [NSString stringWithFormat:@"%s, line %d", fs_err.message, fs_err.line]}];
        if (fs_err.type == ERR_CANCELLED)
            *error = nil;
        free(fs_err.message);
        [NSFileManager.defaultManager removeItemAtURL:tempDestination error:nil];
        return NO;
    }
    if (![NSFileManager.defaultManager moveItemAtURL:tempDestination toURL:destination error:error])
        return NO;

    void (^addRoot)(void) = ^{
        [[self mutableOrderedSetValueForKey:@"roots"] addObject:name];
    };
    if (!NSThread.isMainThread)
        dispatch_sync(dispatch_get_main_queue(), addRoot);
    else
        addRoot();
    return YES;
}

- (BOOL)exportRootNamed:(NSString *)name toArchive:(NSURL *)archive error:(NSError **)error progressReporter:(id<ProgressReporter> _Nullable)progress {
    NSAssert([self.roots containsObject:name], @"trying to export a root that doesn't exist: %@", name);
    struct fakefsify_error fs_err;
    if (!fakefs_export([self rootUrl:name].fileSystemRepresentation,
                       archive.fileSystemRepresentation,
                       &fs_err, (struct progress) {(__bridge void *) progress, root_progress_callback})) {
        NSString *domain = NSPOSIXErrorDomain;
        if (fs_err.type == ERR_SQLITE)
            domain = @"SQLite";
        *error = [NSError errorWithDomain:domain
                                     code:fs_err.code
                                 userInfo:@{NSLocalizedDescriptionKey: [NSString stringWithUTF8String:fs_err.message]}];
        if (fs_err.type == ERR_CANCELLED)
            *error = nil;
        free(fs_err.message);
        return NO;
    }
    return YES;
}

- (BOOL)destroyRootNamed:(NSString *)name error:(NSError **)error {
    NSAssert([self.roots containsObject:name], @"trying to destroy a root that doesn't exist");
    if (![NSFileManager.defaultManager removeItemAtURL:[self rootUrl:name] error:error])
        return NO;
    [[self mutableOrderedSetValueForKey:@"roots"] removeObject:name];
    return YES;
}

- (BOOL)renameRoot:(NSString *)name toName:(NSString *)newName error:(NSError **)error {
    NSAssert([self.roots containsObject:name], @"trying to rename a root that doesn't exist");
    if (![NSFileManager.defaultManager moveItemAtURL:[self rootUrl:name] toURL:[self rootUrl:newName] error:error])
        return NO;
    NSMutableOrderedSet *newRoots = [self.roots mutableCopy];
    NSUInteger index = [newRoots indexOfObject:name];
    newRoots[index] = newName;
    [self setValue:[newRoots copy] forKey:@"roots"];
    if ([self.defaultRoot isEqualToString:name])
        self.defaultRoot = newName;
    return YES;
}

@end

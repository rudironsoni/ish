//
//  Roots.m
//  iSH
//
//  Created by Theodore Dubois on 6/7/20.
//

#import <UIKit/UIKit.h>
#import <FileProvider/FileProvider.h>
#import "Roots.h"
#import "NSObject+SaneKVO.h"
#import "root_store.h"

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
    return ios_roots_production_directory();
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
        NSArray<NSString *> *rootNames = ios_root_store_list(rootsDirectory, &error);
        NSAssert(error == nil, @"couldn't list roots: %@", error);
        self.roots = [rootNames mutableCopy];

        if (!self.roots.count) {
            NSError *importError;
            NSURL *archiveURL = ios_root_default_archive_url();
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
        }];

        if ((!self.defaultRoot || ![self.roots containsObject:self.defaultRoot]) && self.roots.count)
            self.defaultRoot = self.roots.firstObject;
    }
    return self;
}

- (NSString *)defaultRoot {
    return ios_root_store_default_root_name();
}

- (void)setDefaultRoot:(NSString *)defaultRoot {
    ios_root_store_set_default_root_name(defaultRoot);
}

- (NSURL *)rootUrl:(NSString *)name {
    return ios_root_store_url(self.rootsDirectory, name);
}

- (void)syncFileProviderDomains {
    if (NSProcessInfo.processInfo.environment[@"XCTestConfigurationFilePath"] != nil) {
        return;
    }

    if (@available(iOS 11.0, *)) {
        if (![NSFileProviderManager respondsToSelector:@selector(defaultManager)])
            return;
    }
    if (@available(iOS 11.0, *)) {
        NSFileProviderManager *providerManager = [NSFileProviderManager defaultManager];
        if (providerManager == nil)
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
    if (!ios_root_store_import_archive(self.rootsDirectory, archive, name, progress, error))
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
    if (!ios_root_store_export_archive(self.rootsDirectory, name, archive, progress, error)) {
        return NO;
    }
    return YES;
}

- (BOOL)destroyRootNamed:(NSString *)name error:(NSError **)error {
    NSAssert([self.roots containsObject:name], @"trying to destroy a root that doesn't exist");
    if (!ios_root_store_destroy(self.rootsDirectory, name, error))
        return NO;
    [[self mutableOrderedSetValueForKey:@"roots"] removeObject:name];
    return YES;
}

- (BOOL)renameRoot:(NSString *)name toName:(NSString *)newName error:(NSError **)error {
    NSAssert([self.roots containsObject:name], @"trying to rename a root that doesn't exist");
    if (!ios_root_store_rename(self.rootsDirectory, name, newName, error))
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

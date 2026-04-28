//
//  Roots.h
//  iSH
//
//  Created by Theodore Dubois on 6/7/20.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@protocol ProgressReporter

- (void)updateProgress:(double)progressFraction message:(NSString *)progressMessage;
- (BOOL)shouldCancel;

@end

@interface Roots : NSObject

// Factory methods
+ (instancetype)instance; // Production: uses App Group container
+ (instancetype)instanceWithRootsDirectory:(NSURL *)rootsDirectory; // Test: inject local path

@property (readonly) NSOrderedSet<NSString *> *roots;
@property NSString *defaultRoot;
@property (readonly) BOOL wantsVersionFile;
@property (readonly, nonatomic) BOOL lastImportAttempted;
@property (readonly, nonatomic) BOOL lastImportSucceeded;
@property (readonly, nonatomic, nullable) NSString *lastImportErrorDescription;
@property (readonly, nonatomic) BOOL lastArchiveURLPresent;
- (NSURL *)rootUrl:(NSString *)name;
- (BOOL)importRootFromArchive:(NSURL *)archive name:(NSString *)name error:(NSError **)error progressReporter:(id<ProgressReporter> _Nullable)progress;
- (BOOL)exportRootNamed:(NSString *)name toArchive:(NSURL *)archive error:(NSError **)error progressReporter:(id<ProgressReporter> _Nullable)progress;
- (BOOL)destroyRootNamed:(NSString *)name error:(NSError **)error;
- (BOOL)renameRoot:(NSString *)name toName:(NSString *)newName error:(NSError **)error;

@end

NS_ASSUME_NONNULL_END

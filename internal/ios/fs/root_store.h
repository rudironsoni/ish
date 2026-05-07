#ifndef IXLAND_ROOT_STORE_H
#define IXLAND_ROOT_STORE_H

#import <Foundation/Foundation.h>

NSURL *ios_roots_production_directory(void);
NSArray<NSString *> *ios_root_store_list(NSURL *rootsDirectory, NSError **error);
NSURL *ios_root_store_url(NSURL *rootsDirectory, NSString *name);
BOOL ios_root_store_migrate_legacy_layout(NSURL *rootURL, NSError **error);

NSString *ios_root_store_default_root_name(void);
void ios_root_store_set_default_root_name(NSString *defaultRoot);

NSURL *ios_root_default_archive_url(void);

BOOL ios_root_store_import_archive(NSURL *rootsDirectory,
                                   NSURL *archive,
                                   NSString *name,
                                   id progressReporter,
                                   NSError **error);
BOOL ios_root_store_export_archive(NSURL *rootsDirectory,
                                   NSString *name,
                                   NSURL *archive,
                                   id progressReporter,
                                   NSError **error);
BOOL ios_root_store_destroy(NSURL *rootsDirectory, NSString *name, NSError **error);
BOOL ios_root_store_rename(NSURL *rootsDirectory, NSString *name, NSString *newName, NSError **error);

#endif

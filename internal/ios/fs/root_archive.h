#ifndef IXLAND_ROOT_ARCHIVE_H
#define IXLAND_ROOT_ARCHIVE_H

#import <Foundation/Foundation.h>

BOOL ios_root_archive_import(NSURL *archive,
                             NSURL *destination,
                             id progressReporter,
                             NSError **error);

BOOL ios_root_archive_export(NSURL *sourceRoot,
                             NSURL *archive,
                             id progressReporter,
                             NSError **error);

#endif

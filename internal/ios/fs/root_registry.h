#ifndef IXLAND_ROOT_REGISTRY_H
#define IXLAND_ROOT_REGISTRY_H

#import <Foundation/Foundation.h>

NSURL *ios_root_default_url(void);
NSURL *ios_root_default_data_url(void);

BOOL ios_root_has_available_roots(void);
BOOL ios_root_last_archive_url_present(void);
BOOL ios_root_last_import_attempted(void);
BOOL ios_root_last_import_succeeded(void);
NSString *ios_root_last_import_error_description(void);

#endif

#import "root_registry.h"

#import "Roots.h"

NSURL *ios_root_default_url(void) {
    NSString *defaultRoot = Roots.instance.defaultRoot;
    if (defaultRoot == nil) {
        return nil;
    }
    return [Roots.instance rootUrl:defaultRoot];
}

NSURL *ios_root_default_data_url(void) {
    return ios_root_default_url();
}

BOOL ios_root_has_available_roots(void) {
    return Roots.instance.roots.count > 0;
}

BOOL ios_root_last_archive_url_present(void) {
    return Roots.instance.lastArchiveURLPresent;
}

BOOL ios_root_last_import_attempted(void) {
    return Roots.instance.lastImportAttempted;
}

BOOL ios_root_last_import_succeeded(void) {
    return Roots.instance.lastImportSucceeded;
}

NSString *ios_root_last_import_error_description(void) {
    return Roots.instance.lastImportErrorDescription ?: @"";
}

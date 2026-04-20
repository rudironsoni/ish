#ifndef IXLAND_CLANGD_OWNER_POLICY_H
#define IXLAND_CLANGD_OWNER_POLICY_H

#if defined(IXLAND_LINUX_OWNER_FILE) && defined(__OBJC__)
#error "Linux-owner paths must not compile as Objective-C"
#endif

#endif

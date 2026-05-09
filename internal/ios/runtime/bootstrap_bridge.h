#ifndef IXLAND_IOS_BOOTSTRAP_BRIDGE_H
#define IXLAND_IOS_BOOTSTRAP_BRIDGE_H

#include <stdbool.h>

struct runtime_bootstrap_diagnostics {
    bool root_present;
    bool root_exists;
    bool root_data_exists;
    bool roots_available;
    bool archive_url_present;
    bool import_attempted;
    bool import_succeeded;
    bool root_mount_called;
    bool mounts_non_empty_after_root_mount;
    bool become_first_process_called;
    bool pid1_exists_after_become_first_process;
    int root_mount_return_value;
    int become_first_process_return_value;
    int bootstrap_return_value;
    const char *import_error_description;
};

int runtime_bootstrap_session(void);
int runtime_boot_error(void);
void runtime_get_bootstrap_diagnostics(struct runtime_bootstrap_diagnostics *diagnostics);
int runtime_finish_post_mount_setup(void);
void runtime_install_process_hooks(void);
void runtime_configure_socket_prefix(void);

#endif

/*
 * TCTI Boundary Probe - Test-only support
 *
 * This is test-only code, NOT product code.
 * Holds the active probe buffer pointer for assembly probes.
 */

#include "tcti_boundary_probe.h"

#include <stddef.h>

/*
 * Global pointer to active probe buffer - accessed by assembly probes
 * Must be global (not static) so assembly can reference it via _active_probe_buffer
 */
struct tcti_boundary_probe_buffer *active_probe_buffer = NULL;

void tcti_boundary_probe_set_buffer(struct tcti_boundary_probe_buffer *buf)
{
    active_probe_buffer = buf;
}

void tcti_boundary_probe_clear_buffer(void)
{
    active_probe_buffer = NULL;
}

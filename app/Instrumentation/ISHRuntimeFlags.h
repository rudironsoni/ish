//
//  ISHRuntimeFlags.h
//  iSH
//
//  Compile-time flags for iSH runtime behavior.
//

#ifndef ISHRuntimeFlags_h
#define ISHRuntimeFlags_h

// Single runtime mode - no contradictory flags
// This replaces the previous boolean flag model (DISABLE_EMULATION, ALLOW_SESSION_BOOTSTRAP,
// FULL_RUNTIME)
typedef enum {
    ISH_RUNTIME_MODE_SHELL_ONLY = 0,        // No session infrastructure
    ISH_RUNTIME_MODE_SESSION_BOOTSTRAP = 1, // Init child + stdio, no exec/start
    ISH_RUNTIME_MODE_SESSION_EXEC = 2,      // + do_execve, no task_start
    ISH_RUNTIME_MODE_FULL_GUEST = 3         // Full guest execution
} ish_runtime_mode_t;

// Compile-time mode selection - change this to switch modes
// APPSIM-004: FULL_GUEST mode for shell/login readiness validation
// This enables complete guest execution including task_start() and CPU run loop
#define ISH_RUNTIME_MODE ISH_RUNTIME_MODE_FULL_GUEST

#endif /* ISHRuntimeFlags_h */

//
//  ISHRuntimeFlags.h
//  iSH
//
//  Compile-time flags for iSH runtime behavior.
//

#ifndef ISHRuntimeFlags_h
#define ISHRuntimeFlags_h

// Task Zero: When set to 1, disables Linux/emulator startup
// so the app shell can boot and be tested independently.
// This is the single source of truth for guest startup bypass.
#define ISH_TASK_ZERO_DISABLE_EMULATION 1

// Task Zero Session Bootstrap: When set to 1, allows session bootstrap
// (become_new_init_child, PTY creation, create_stdio, do_execve)
// but STILL BLOCKS task_start(current). This is the smallest safe boundary
// immediately before guest runtime.
#define ISH_TASK_ZERO_ALLOW_SESSION_BOOTSTRAP 1

// Task Zero Full Runtime: When set to 1, allows full guest execution including
// task_start(current). This completes the runtime reintroduction - the emulator
// will actually execute guest code. When 0, task_start is blocked for UI testing.
#define ISH_TASK_ZERO_FULL_RUNTIME 1

#endif /* ISHRuntimeFlags_h */

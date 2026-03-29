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

#endif /* ISHRuntimeFlags_h */

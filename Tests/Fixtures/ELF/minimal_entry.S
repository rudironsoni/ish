/*
 * Minimal AArch64 entry point for guest execution proof
 * Executes valid instructions and exits via syscall
 * This fixture is designed for B4 execution boundary proof:
 * - Loads PT_LOAD segments (B2)
 * - Initializes PC/SP (B3)
 * - Executes instructions and reaches syscall boundary (B4)
 */

    .global _start
    .type _start, %function
    .align 4

_start:
    // Initialize stack frame
    mov     x29, sp
    
    // Execute at least one valid instruction
    // This proves the first instruction boundary was reached
    mov     x0, #42           // Set return value (x0 = 42)
    
    // Second instruction to prove continued execution
    mov     x1, #0            // Clear x1
    
    // Third instruction
    add     x0, x0, x1        // x0 = 42 + 0 = 42
    
    // Syscall exit - triggers SVC which is handled by runtime
    // Syscall 93 = exit, x0 = exit code
    mov     x8, #93           // syscall number for exit
    mov     x0, #0            // exit code 0
    svc     #0                // Supervisor call - handled by runtime

    // Should never reach here if syscall handled
    // If we do, infinite loop to prevent undefined behavior
1:
    b       1b

    .size _start, . - _start

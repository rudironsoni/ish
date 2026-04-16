/*
 * Minimal dynamic AArch64 ELF for Milestone B
 * Has PT_INTERP pointing to interpreter path
 * Executes simple instructions and exits via exit_group syscall
 * Used to prove real dynamic execution through interpreter path
 */

    .global _start
    .type _start, %function
    .align 4

_start:
    // Initialize stack frame
    mov     x29, sp
    
    // Execute valid instructions to prove execution boundary
    mov     x0, #42           // x0 = 42 (return value)
    mov     x1, #0            // x1 = 0
    add     x0, x0, x1        // x0 = 42 + 0 = 42
    
    // exit_group syscall (231)
    mov     x8, #231          // syscall number for exit_group
    mov     x0, #0            // exit code 0
    svc     #0

    // Should never reach here
1:
    b       1b

    .size _start, . - _start

/*
 * Build command for dynamic ELF:
 * aarch64-linux-gnu-gcc -nostdlib -static-pie -o dynamic_minimal.elf dynamic_minimal.S
 * Or with PT_INTERP:
 * aarch64-linux-gnu-gcc -nostdlib -o dynamic_minimal.elf dynamic_minimal.S
 * 
 * For test harness, we need PT_INTERP in the ELF header
 */

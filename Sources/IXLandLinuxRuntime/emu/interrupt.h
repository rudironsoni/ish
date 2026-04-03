#ifndef EMU_INTERRUPT_H
#define EMU_INTERRUPT_H

// AArch64 exception/signal constants for TCTI
// Maps to iSH signal numbers

#define INT_GPF 13           // SIGILL - Illegal instruction
#define INT_UNDEFINED 6      // SIGABRT - Undefined instruction
#define INT_SYSCALL 128      // SIGSYSCALL - Syscall trap
#define INT_TRAP 5           // SIGTRAP - Debug trap
#define INT_BREAKPOINT 5     // SIGTRAP - Breakpoint
#define INT_DEBUG 5          // SIGTRAP - Debug trap
#define INT_TIMER 14         // SIGALRM - Timer

// Signal numbers for TCTI exit reasons
#define INT_DIV0 8           // SIGFPE - Division by zero
#define INT_UD 6             // SIGABRT - Abort

#endif

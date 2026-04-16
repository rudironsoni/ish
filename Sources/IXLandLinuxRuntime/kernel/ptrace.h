#ifndef KERNEL_PTRACE_H
#define KERNEL_PTRACE_H

#import <IXLandLinuxRuntime/util/misc.h>

#define PTRACE_TRACEME_    0
#define PTRACE_PEEKTEXT_   1
#define PTRACE_PEEKDATA_   2
#define PTRACE_PEEKUSER_   3
#define PTRACE_POKETEXT_   4
#define PTRACE_POKEDATA_   5
#define PTRACE_CONT_       7
#define PTRACE_KILL_       8
#define PTRACE_SINGLESTEP_ 9
#define PTRACE_GETREGS_    12
#define PTRACE_SETREGS_    13
#define PTRACE_GETFPREGS_  14
#define PTRACE_SETFPREGS_  15
#define PTRACE_SETOPTIONS_ 0x4200
#define PTRACE_GETSIGINFO_ 0x4202

#define PTRACE_EVENT_FORK_ 1

struct user_regs_struct_ {
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t eax;
    uint32_t xds;
    uint32_t xes;
    uint32_t xfs;
    uint32_t xgs;
    uint32_t orig_eax;
    uint32_t eip;
    uint32_t xcs;
    uint32_t eflags;
    uint32_t esp;
    uint32_t xss;
};

struct user_fpregs_struct_ {
    uint32_t cwd;
    uint32_t swd;
    uint32_t twd;
    uint32_t fip;
    uint32_t fcs;
    uint32_t foo;
    uint32_t fos;
    uint32_t st_space[20];
};

struct user_ {
    struct user_regs_struct_ user_regs;
    char padding[286 - sizeof(struct user_regs_struct_)];
};

uint32_t sys_ptrace(uint32_t request, uint32_t pid, addr_t addr, uint32_t data);

#endif /* KERNEL_PTRACE_H */

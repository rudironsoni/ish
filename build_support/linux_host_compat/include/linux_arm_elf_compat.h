#ifndef IXLAND_MACOS_LINUX_ARM_ELF_COMPAT_H
#define IXLAND_MACOS_LINUX_ARM_ELF_COMPAT_H

/*
 * Linux 6.12 ARM relocation values needed by macOS host tools during
 * prepare/modules_prepare. These are not vendored Linux headers; they only
 * unblock host-side helper compilation.
 */

#ifndef R_ARM_PC24
#define R_ARM_PC24 1
#endif
#ifndef R_ARM_ABS32
#define R_ARM_ABS32 2
#endif
#ifndef R_ARM_REL32
#define R_ARM_REL32 3
#endif
#ifndef R_ARM_THM_PC22
#define R_ARM_THM_PC22 10
#endif
#ifndef R_ARM_THM_CALL
#define R_ARM_THM_CALL 10
#endif
#ifndef R_ARM_CALL
#define R_ARM_CALL 28
#endif
#ifndef R_ARM_JUMP24
#define R_ARM_JUMP24 29
#endif
#ifndef R_ARM_THM_JUMP24
#define R_ARM_THM_JUMP24 30
#endif
#ifndef R_ARM_MOVW_ABS_NC
#define R_ARM_MOVW_ABS_NC 43
#endif
#ifndef R_ARM_MOVT_ABS
#define R_ARM_MOVT_ABS 44
#endif
#ifndef R_ARM_THM_MOVW_ABS_NC
#define R_ARM_THM_MOVW_ABS_NC 47
#endif
#ifndef R_ARM_THM_MOVT_ABS
#define R_ARM_THM_MOVT_ABS 48
#endif
#ifndef R_ARM_THM_JUMP19
#define R_ARM_THM_JUMP19 51
#endif

#endif

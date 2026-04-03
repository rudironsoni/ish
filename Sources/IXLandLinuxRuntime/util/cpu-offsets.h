/* CPU offsets for iSH - x86 style for Linux/assembly compatibility */
/* These map x86 register names to aarch64 struct cpu_state offsets */

/* x86 GPR offsets (map to aarch64 x[0]-x[6]) */
#define CPU_eax 16    /* offsetof(struct cpu_state, x[0]) */
#define CPU_ebx 24    /* offsetof(struct cpu_state, x[1]) */
#define CPU_ecx 32    /* offsetof(struct cpu_state, x[2]) */
#define CPU_edx 40    /* offsetof(struct cpu_state, x[3]) */
#define CPU_esi 48    /* offsetof(struct cpu_state, x[4]) */
#define CPU_edi 56    /* offsetof(struct cpu_state, x[5]) */
#define CPU_ebp 64    /* offsetof(struct cpu_state, x[6]) */
#define CPU_esp 264   /* offsetof(struct cpu_state, sp) */
#define CPU_eip 272   /* offsetof(struct cpu_state, pc) */

/* Additional fields */
#define CPU_poked_ptr 832       /* offsetof(struct cpu_state, poked_ptr) */
#define CPU_segfault_addr 816   /* offsetof(struct cpu_state, fault_addr) */
#define CPU_segfault_was_write 824 /* offsetof(struct cpu_state, fault_was_write) */

/* Fiber block fields */
#define FIBER_BLOCK_code 64
#define FIBER_BLOCK_addr 0

/* Local block tracking */
#define LOCAL_last_block 0

/* TLB entries offset */
#define TLB_entries 0
#define TLB_ENTRY_page 0
#define TLB_ENTRY_page_if_writable 4
#define TLB_ENTRY_data_minus_addr 8
#define TLB_dirty_page 8

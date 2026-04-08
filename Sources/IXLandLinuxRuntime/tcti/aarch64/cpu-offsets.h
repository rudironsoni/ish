/* aarch64 CPU offsets for asm/inline-asm access */
#define CPU_OFFSET_mmu              0
#define CPU_OFFSET_cycle            8
#define CPU_OFFSET_x                16
#define CPU_OFFSET_sp               264
#define CPU_OFFSET_pc               272
#define CPU_OFFSET_pstate           280
#define CPU_OFFSET_vregs            288
#define CPU_OFFSET_fpcr             800
#define CPU_OFFSET_fpsr             804
#define CPU_OFFSET_tpidr_el0        808
#define CPU_OFFSET_fault_addr       816
#define CPU_OFFSET_fault_was_write  824
#define CPU_OFFSET_poked_ptr        832
#define CPU_OFFSET__poked           840
#define CPU_OFFSET_exec_ctx         848
#define CPU_OFFSET_trapno           856
#define CPU_OFFSET_tcti_exit_reason 860
#define CPU_OFFSET_tlb              864
#define CPU_OFFSET_exclusive_addr   872
#define CPU_OFFSET_exclusive_size   880
#define CPU_OFFSET_exclusive_valid  884

#define CPU_OFFSET_x0  (CPU_OFFSET_x + 0 * 8)
#define CPU_OFFSET_x1  (CPU_OFFSET_x + 1 * 8)
#define CPU_OFFSET_x2  (CPU_OFFSET_x + 2 * 8)
#define CPU_OFFSET_x3  (CPU_OFFSET_x + 3 * 8)
#define CPU_OFFSET_x4  (CPU_OFFSET_x + 4 * 8)
#define CPU_OFFSET_x5  (CPU_OFFSET_x + 5 * 8)
#define CPU_OFFSET_x6  (CPU_OFFSET_x + 6 * 8)
#define CPU_OFFSET_x7  (CPU_OFFSET_x + 7 * 8)
#define CPU_OFFSET_x8  (CPU_OFFSET_x + 8 * 8)
#define CPU_OFFSET_x9  (CPU_OFFSET_x + 9 * 8)
#define CPU_OFFSET_x10 (CPU_OFFSET_x + 10 * 8)
#define CPU_OFFSET_x11 (CPU_OFFSET_x + 11 * 8)
#define CPU_OFFSET_x12 (CPU_OFFSET_x + 12 * 8)
#define CPU_OFFSET_x13 (CPU_OFFSET_x + 13 * 8)
#define CPU_OFFSET_x14 (CPU_OFFSET_x + 14 * 8)
#define CPU_OFFSET_x15 (CPU_OFFSET_x + 15 * 8)
#define CPU_OFFSET_x16 (CPU_OFFSET_x + 16 * 8)
#define CPU_OFFSET_x30 (CPU_OFFSET_x + 30 * 8)

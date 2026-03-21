#include <stdbool.h>
#include <stdlib.h>

#include <linux/threads.h>
#include <asm/ptrace.h>
#include <emu/exec.h>
#include <emu/kernel.h>
#include "../kernel/irq_user.h"

#include "emu/cpu.h"
#include "emu/tlb.h"
#include "emu/interrupt.h"
#define ENGINE_ASBESTOS 1
#include "asbestos/asbestos.h"

extern int current_pid(void);

struct emu_mm_ctx {
	struct mmu mmu;
	struct emu_mm *emu_mm;
};

static __thread struct tlb the_tlb;

static void *ishemu_translate(struct mmu *mem, addr_t addr, int type)
{
	struct emu_mm *emu_mm = container_of(mem, struct emu_mm_ctx, mmu)->emu_mm;
	bool writable;
	void *ptr = user_to_kernel_emu(emu_mm, addr, &writable);
	if (ptr && type == MEM_WRITE && !writable) {
		ptr = NULL;
	}
	return ptr;
}

static struct mmu_ops ishemu_ops = {
	.translate = ishemu_translate,
};

static bool poke[NR_CPUS];

/*
 * Register mapping from aarch64 cpu_state to x86 pt_regs
 * This is needed because the Linux kernel being emulated is x86
 * while the host (iOS) is aarch64
 */
static void cpu_to_pt_regs(struct cpu_state *cpu, struct pt_regs *regs)
{
	// Map aarch64 registers to x86 registers
	// x0-x5 map to ax, bx, cx, dx, si, di
	// x6 maps to bp, x7-x30 need special handling
	regs->ax = cpu->x[0];
	regs->bx = cpu->x[1];
	regs->cx = cpu->x[2];
	regs->dx = cpu->x[3];
	regs->si = cpu->x[4];
	regs->di = cpu->x[5];
	regs->bp = cpu->x[6];
	// Note: x7-x29 are callee-saved in aarch64 but don't have direct x86 equivalents
	// They are stored in the fiber_frame for restoration
	regs->sp = cpu->sp;
	regs->ip = cpu->pc;
	regs->flags = cpu->pstate;
	regs->tls = cpu->tpidr_el0;
	// fault_addr stored in cr2 for x86 compatibility
}

static void pt_regs_to_cpu(struct pt_regs *regs, struct cpu_state *cpu)
{
	// Map x86 registers back to aarch64
	cpu->x[0] = regs->ax;
	cpu->x[1] = regs->bx;
	cpu->x[2] = regs->cx;
	cpu->x[3] = regs->dx;
	cpu->x[4] = regs->si;
	cpu->x[5] = regs->di;
	cpu->x[6] = regs->bp;
	cpu->sp = regs->sp;
	cpu->pc = regs->ip;
	cpu->pstate = regs->flags;
	cpu->tpidr_el0 = regs->tls;
	// fault_addr not stored in pt_regs, handled separately
}

static void emu_run_to_interrupt(struct emu *emu, struct cpu_state *cpu)
{
	struct pt_regs *regs = emu_pt_regs(emu);
	struct emu_mm_ctx *mm_ctx = emu->mm->ctx;

	cpu->mmu = &mm_ctx->mmu;
	
	// Copy registers from pt_regs to cpu
	pt_regs_to_cpu(regs, cpu);
	
	cpu->poked_ptr = &poke[get_smp_processor_id()];

	int interrupt = cpu_run_to_interrupt(cpu, &the_tlb);

	// Copy back to pt_regs
	cpu_to_pt_regs(cpu, regs);

	if (interrupt == INT_GPF) {
		regs->cr2 = cpu->fault_addr;
		regs->error_code = cpu->fault_was_write << 1;
	} else {
		regs->cr2 = regs->error_code = 0;
	}
	regs->trap_nr = interrupt;
}

void emu_run(struct emu *emu)
{
	struct cpu_state cpu = {};
	if (emu->snapshot) {
		struct cpu_state *snapshot = emu->snapshot;
		cpu = *snapshot;
		free(snapshot);
		emu->snapshot = NULL;
	}
	emu->ctx = &cpu;
	for (;;) {
		emu_run_to_interrupt(emu, &cpu);
		handle_cpu_trap(emu);
	}
}

void emu_finish_fork(struct emu *emu)
{
	struct cpu_state *cpu = emu->ctx;
	struct cpu_state *snapshot = emu->snapshot = calloc(1, sizeof(*snapshot));
	*snapshot = *cpu;
	emu->ctx = NULL;
}

void emu_destroy(struct emu *emu)
{
}

void emu_poke_cpu(int cpu)
{
	__atomic_store_n(&poke[cpu], true, __ATOMIC_SEQ_CST);
}

void emu_flush_tlb_local(struct emu_mm *mm, unsigned long start, unsigned long end)
{
	if (the_tlb.mmu == NULL)
		return;
	tlb_flush(&the_tlb);
	struct emu_mm_ctx *mm_ctx = mm->ctx;
	if (mm_ctx->mmu.asbestos != NULL)
		asbestos_invalidate_range(mm_ctx->mmu.asbestos, start / PAGE_SIZE, (end + PAGE_SIZE - 1) / PAGE_SIZE /* TODO DIV_ROUND_UP? */);
}

void emu_mmu_init(struct emu_mm *mm)
{
	struct emu_mm_ctx *mm_ctx = mm->ctx = calloc(1, sizeof(*mm_ctx));
	mm_ctx->emu_mm = mm;
	mm_ctx->mmu.asbestos = asbestos_new(&mm_ctx->mmu);
	mm_ctx->mmu.ops = &ishemu_ops;
}

void emu_mmu_destroy(struct emu_mm *mm)
{
	struct emu_mm_ctx *mm_ctx = mm->ctx;
	asbestos_free(mm_ctx->mmu.asbestos);
	mm_ctx->mmu.asbestos = NULL;
}

void emu_switch_mm(struct emu *emu, struct emu_mm *mm)
{
	struct emu_mm_ctx *mm_ctx = mm->ctx;
	tlb_refresh(&the_tlb, &mm_ctx->mmu);
}

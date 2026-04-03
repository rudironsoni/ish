/*
 * aarch64 VDSO and signal trampoline
 *
 * The vdso contains:
 * - sigtramp code for signal return
 * - __kernel_rt_sigreturn entry point
 */

#import <IXLandLinuxRuntime/kernel/aarch64/vdso.h>

#import <IXLandLinuxRuntime/kernel/aarch64/signal.h>

#include <string.h>

// Signal trampoline code for rt_sigreturn
// This is mapped into every process and used as the return address
// for signal handlers
static const uint32_t vdso_sigtramp_code[] = {
    // mov x8, #139 (A64_NR_rt_sigreturn)
    // svc #0
    0xd2801168, // mov x8, #139 (0x8b = 139)
    0xd4000001, // svc #0
};

// The vdso page layout
struct vdso_data {
    // Signal trampoline at offset 0
    uint32_t sigtramp[2];
    // Padding to page boundary
    uint8_t padding[4096 - sizeof(uint32_t) * 2];
};

static struct vdso_data vdso_page;

int a64_init_vdso(void)
{
    memset(&vdso_page, 0, sizeof(vdso_page));

    // Copy sigtramp code
    memcpy(vdso_page.sigtramp, vdso_sigtramp_code, sizeof(vdso_sigtramp_code));

    return 0;
}

uint64_t a64_get_vdso_sigtramp(void)
{
    // The sigtramp is at the beginning of the vdso page
    return A64_VDSO_BASE;
}

// Get the vdso page for mapping into processes
const void *a64_get_vdso_page(size_t *size)
{
    if (size) {
        *size = sizeof(vdso_page);
    }
    return &vdso_page;
}

// Setup vdso in a new process's address space
int a64_setup_vdso(struct mm *mm)
{
    (void)mm; // Placeholder - VDSO mapping not yet implemented
    // Map the vdso page at A64_VDSO_BASE
    // This would call the memory mapping functions
    // For now, this is a placeholder

    // struct page *page = alloc_page();
    // if (!page) return -ENOMEM;

    // memcpy(page_to_virt(page), &vdso_page, sizeof(vdso_page));
    // map_page(mm, A64_VDSO_BASE, page, PAGE_READ | PAGE_EXEC);

    return 0;
}

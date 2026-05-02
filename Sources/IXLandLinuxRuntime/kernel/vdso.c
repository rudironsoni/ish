#import <IXLandLinuxRuntime/kernel/elf.h>
#import <IXLandLinuxRuntime/kernel/vdso.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
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
struct vdso_page_layout {
    // Signal trampoline at offset 0
    uint32_t sigtramp[2];
    // Padding to page boundary
    uint8_t padding[4096 - sizeof(uint32_t) * 2];
};

static struct vdso_page_layout vdso_page;

// VDSO data buffer - exposed for vdso_symbol() ELF parsing
// Note: The original .incbin approach required a pre-built vDSO ELF binary
// that was never committed to the repo. This C-based approach provides the
// same interface without the external dependency.
const char vdso_data[VDSO_PAGES * (1 << 12)];

bool vdso_has_elf_image(void)
{
    const struct elf_header *header = (const struct elf_header *)vdso_data;
    if (memcmp(&header->magic, ELF_MAGIC, 4) != 0)
        return false;
    if (header->bitness != ELF_64BIT || header->endian != ELF_LITTLEENDIAN)
        return false;
    if (header->machine != ELF_AARCH64)
        return false;
    if (header->header_size < sizeof(struct elf_header))
        return false;
    if (header->phent_size != sizeof(struct prg_header))
        return false;
    if (header->phent_count == 0)
        return false;
    if (header->prghead_off + (elf_off_t)header->phent_count * sizeof(struct prg_header) >
        sizeof(vdso_data))
        return false;
    return true;
}

int vdso_init(void)
{
    memset(&vdso_page, 0, sizeof(vdso_page));
    memcpy(vdso_page.sigtramp, vdso_sigtramp_code, sizeof(vdso_sigtramp_code));
    return 0;
}

int vdso_symbol(const char *name)
{
    // For now, return sigtramp address for rt_sigreturn
    // Full ELF parsing would require a pre-built vDSO binary
    if (strcmp(name, "__kernel_rt_sigreturn") == 0) {
        return 0; // sigtramp is at offset 0 in vdso page
    }
    return 0;
}

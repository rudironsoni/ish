#ifndef KERNEL_VDSO_H
#define KERNEL_VDSO_H

#include <stdbool.h>

// VDSO size in pages - AArch64 VDSO is typically 1 page
#ifndef VDSO_PAGES
#define VDSO_PAGES 1
#endif

// VVAR (VDSO variables) pages - typically 1 page for vvar region
#ifndef VVAR_PAGES
#define VVAR_PAGES 1
#endif

extern const char vdso_data[VDSO_PAGES * (1 << 12)] __asm__("vdso_data");
bool vdso_has_elf_image(void);
int vdso_symbol(const char *name);

#endif

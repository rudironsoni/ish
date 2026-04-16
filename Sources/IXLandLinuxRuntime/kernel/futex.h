#ifndef KERNEL_FUTEX_H
#define KERNEL_FUTEX_H

int futex_wake(addr_t uaddr, uint32_t val);

#endif

#import <IXLandLinuxRuntime/kernel/calls.h>

int64_t sys_ipc(uint64_t call, int64_t first, int64_t second, int64_t third, addr_t ptr,
                int64_t fifth)
{
    STRACE("ipc(%u, %d, %d, %d, %#x, %d)", call, first, second, third, ptr, fifth);
    return _ENOSYS;
}

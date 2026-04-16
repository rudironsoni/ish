#import <IXLandLinuxRuntime/kernel/calls.h>
#include <fcntl.h>

#ifdef __APPLE__
#include <CommonCrypto/CommonCrypto.h>
#include <CommonCrypto/CommonRandom.h>
#else
#include <linux/random.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

int get_random(char *buf, size_t len)
{
#ifdef __APPLE__
    return CCRandomGenerateBytes(buf, len) != kCCSuccess;
#else
    return syscall(SYS_getrandom, buf, len, 0) < 0;
#endif
}

int32_t sys_getrandom(addr_t buf_addr, uint32_t len, uint32_t UNUSED(flags))
{
    if (len > 1 << 20)
        return _EIO;
    char *buf = malloc(len);
    if (get_random(buf, len) != 0) {
        free(buf);
        return _EIO;
    }
    if (user_write(buf_addr, buf, len)) {
        free(buf);
        return _EFAULT;
    }
    free(buf);
    return len;
}

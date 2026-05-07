#ifndef IXLAND_MACOS_BYTESWAP_H
#define IXLAND_MACOS_BYTESWAP_H

#include <libkern/OSByteOrder.h>

#define bswap_16(x) OSSwapInt16((x))
#define bswap_32(x) OSSwapInt32((x))
#define bswap_64(x) OSSwapInt64((x))

#endif

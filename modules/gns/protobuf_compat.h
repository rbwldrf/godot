#ifndef PROTOBUF_COMPAT_H
#define PROTOBUF_COMPAT_H

// Compatibility layer for protobuf endian functions to work around GCC 15.1.0 conflicts

#include <cstdint>

namespace google {
namespace protobuf {
namespace internal {

// Simple endian conversion functions
namespace little_endian {
    inline uint16_t ToHost(uint16_t value) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return __builtin_bswap16(value);
#else
        return value;
#endif
    }
    
    inline uint32_t ToHost(uint32_t value) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return __builtin_bswap32(value);
#else
        return value;
#endif
    }
    
    inline uint64_t ToHost(uint64_t value) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return __builtin_bswap64(value);
#else
        return value;
#endif
    }
}

}
}
}

#endif // PROTOBUF_COMPAT_H
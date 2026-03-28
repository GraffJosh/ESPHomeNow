#pragma once
#include <algorithm>  // for std::min
#include <cstring>    // for memcpy
#include <string>
inline void safe_copy(char *dst, const std::string &src, size_t dst_size) {
    size_t len = std::min(src.size(), dst_size - 1);
    memcpy(dst, src.data(), len);
    dst[len] = '\0';
}

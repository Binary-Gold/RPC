#include "compress_data/zstd_compress.hpp"
#include <cstddef>
#include <cstdint>

namespace cookrpc {

ZstdCompress::~ZstdCompress() {
}

bool ZstdCompress::CompressString(const std::string& src, std::string& dst, Level level) {
    if (src.empty()) {
        dst.clear();
        return true;
    }
    size_t const compressed_size = ZSTD_compressBound(src.size());
    dst.resize(compressed_size);
    size_t const actual_size = ZSTD_compress(dst.data(), 
                                compressed_size, 
                                        src.data(), src.size(), 
                            static_cast<int>(level));
    if (ZSTD_isError(actual_size)) {
        return false;
    }
    dst.resize(actual_size);
    return true;
}

bool ZstdCompress::DecompressString(const std::string& src, std::string& dst) {
    if (src.empty()) {
        dst.clear();
        return true;
    }

    uint64_t const decompressed_size = ZSTD_getFrameContentSize(src.data(), src.size());
    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR || decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        return false;
    }
    dst.resize(decompressed_size);
    size_t const actual_size = ZSTD_decompress(dst.data(), 
                                        decompressed_size, 
                                        src.data(), src.size());
    if (ZSTD_isError(actual_size)) {
        return false;
    }
    dst.resize(actual_size);
    return true;
}

} // namespace cookrpc

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <zstd.h>
#include <mutex>

namespace cookrpc
{
    enum class Level
    {
        FASTEST = 1, ///< 最快压缩速度，最低压缩率
        DEFAULT = 3, ///< 默认压缩级别，平衡速度和压缩率
        BETTER = 7,  ///< 较好的压缩率，中等速度
        BEST = 19,   ///< 最佳压缩率，但仍保持合理速度
        MAX = 22     ///< 最大压缩率，速度最慢
    };
    class ZstdCompress
    {
    public:
        static ZstdCompress &getInstance()
        {
            static ZstdCompress instance;
            return instance;
        }

        bool CompressString(const std::string &src, std::string &dst, Level level = Level::DEFAULT);

        bool DecompressString(const std::string &src, std::string &dst);

        ZstdCompress(const ZstdCompress &) = delete;
        ZstdCompress &operator=(const ZstdCompress &) = delete;
        
        ZstdCompress(ZstdCompress &&) = delete;
        ZstdCompress &operator=(ZstdCompress &&) = delete;

    private:
        ZstdCompress() = default;
        ~ZstdCompress();
    };
}
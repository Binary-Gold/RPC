#include <gtest/gtest.h>

#include <string>

#include "compress_data/zstd_compress.hpp"

using namespace cookrpc;

class ZstdCompressTest : public ::testing::Test {
protected:
    ZstdCompress& zstd_ = ZstdCompress::getInstance();
};

// ============================================================
// Compress → Decompress 往返
// ============================================================
TEST_F(ZstdCompressTest, RoundTripSimpleString) {
    std::string compressed;
    ASSERT_TRUE(zstd_.CompressString("hello world", compressed));
    EXPECT_GT(compressed.size(), 0u);

    std::string decompressed;
    ASSERT_TRUE(zstd_.DecompressString(compressed, decompressed));
    EXPECT_EQ(decompressed, "hello world");
}

TEST_F(ZstdCompressTest, RoundTripEmptyString) {
    std::string compressed;
    ASSERT_TRUE(zstd_.CompressString("", compressed));
    EXPECT_TRUE(compressed.empty());

    std::string decompressed;
    ASSERT_TRUE(zstd_.DecompressString(compressed, decompressed));
    EXPECT_TRUE(decompressed.empty());
}

TEST_F(ZstdCompressTest, RoundTripLargeData) {
    std::string original(10000, 'A');
    for (size_t i = 0; i < original.size(); ++i) {
        original[i] = static_cast<char>('A' + (i % 26));
    }

    std::string compressed;
    ASSERT_TRUE(zstd_.CompressString(original, compressed));
    EXPECT_LT(compressed.size(), original.size());  // 重复内容能压缩

    std::string decompressed;
    ASSERT_TRUE(zstd_.DecompressString(compressed, decompressed));
    EXPECT_EQ(decompressed, original);
}

TEST_F(ZstdCompressTest, RoundTripBinaryData) {
    std::string original;
    for (int i = 0; i < 256; ++i) {
        original.push_back(static_cast<char>(i));
    }

    std::string compressed;
    ASSERT_TRUE(zstd_.CompressString(original, compressed));

    std::string decompressed;
    ASSERT_TRUE(zstd_.DecompressString(compressed, decompressed));
    EXPECT_EQ(decompressed, original);
}

// ============================================================
// 不同压缩级别
// ============================================================
TEST_F(ZstdCompressTest, CompressionLevelsProduceValidOutput) {
    std::string original = "compress me at different levels";
    std::string compressed;

    EXPECT_TRUE(zstd_.CompressString(original, compressed, Level::FASTEST));
    std::string d1;
    ASSERT_TRUE(zstd_.DecompressString(compressed, d1));
    EXPECT_EQ(d1, original);

    EXPECT_TRUE(zstd_.CompressString(original, compressed, Level::BEST));
    std::string d2;
    ASSERT_TRUE(zstd_.DecompressString(compressed, d2));
    EXPECT_EQ(d2, original);
}

// ============================================================
// 错误路径
// ============================================================
TEST_F(ZstdCompressTest, DecompressInvalidDataFails) {
    std::string garbage = "not compressed data";
    std::string dst;
    EXPECT_FALSE(zstd_.DecompressString(garbage, dst));
}

TEST_F(ZstdCompressTest, DecompressTruncatedDataFails) {
    std::string original = "truncate me";
    std::string compressed;
    ASSERT_TRUE(zstd_.CompressString(original, compressed));

    // 截断压缩数据
    compressed.resize(compressed.size() / 2);
    std::string dst;
    EXPECT_FALSE(zstd_.DecompressString(compressed, dst));
}

// ============================================================
// 单例
// ============================================================
TEST_F(ZstdCompressTest, SingletonReturnsSameInstance) {
    ZstdCompress& a = ZstdCompress::getInstance();
    ZstdCompress& b = ZstdCompress::getInstance();
    EXPECT_EQ(&a, &b);
}

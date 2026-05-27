#include <gtest/gtest.h>

#include <string>

#include "encrypt/aes_encrypt.hpp"

using namespace cookrpc;

class AesEncryptTest : public ::testing::Test {
protected:
    AesEncrypt& aes_ = AesEncrypt::getInstance();
};

// ============================================================
// Encrypt → Decrypt 往返
// ============================================================
TEST_F(AesEncryptTest, RoundTripSimpleString) {
    std::string encrypted;
    ASSERT_TRUE(aes_.Encrypt("hello world", encrypted));
    EXPECT_GT(encrypted.size(), 0u);

    std::string decrypted;
    ASSERT_TRUE(aes_.Decrypt(encrypted, decrypted));
    EXPECT_EQ(decrypted, "hello world");
}

TEST_F(AesEncryptTest, RoundTripEmptyStringFails) {
    std::string encrypted;
    EXPECT_FALSE(aes_.Encrypt("", encrypted));
}

TEST_F(AesEncryptTest, RoundTripLongString) {
    std::string original(1000, 'X');
    for (size_t i = 0; i < original.size(); ++i) {
        original[i] = static_cast<char>('A' + (i % 26));
    }

    std::string encrypted;
    ASSERT_TRUE(aes_.Encrypt(original, encrypted));

    std::string decrypted;
    ASSERT_TRUE(aes_.Decrypt(encrypted, decrypted));
    EXPECT_EQ(decrypted, original);
}

TEST_F(AesEncryptTest, RoundTripBinaryData) {
    std::string original;
    for (int i = 0; i < 256; ++i) {
        original.push_back(static_cast<char>(i));
    }

    std::string encrypted;
    ASSERT_TRUE(aes_.Encrypt(original, encrypted));

    std::string decrypted;
    ASSERT_TRUE(aes_.Decrypt(encrypted, decrypted));
    EXPECT_EQ(decrypted, original);
}

TEST_F(AesEncryptTest, EncryptProducesDifferentOutputEachTime) {
    std::string original = "same input";
    std::string e1, e2;

    ASSERT_TRUE(aes_.Encrypt(original, e1));
    ASSERT_TRUE(aes_.Encrypt(original, e2));

    // 相同明文两次加密结果不同（随机 session key）
    EXPECT_NE(e1, e2);

    // 但都能正确解密
    std::string d1, d2;
    ASSERT_TRUE(aes_.Decrypt(e1, d1));
    ASSERT_TRUE(aes_.Decrypt(e2, d2));
    EXPECT_EQ(d1, original);
    EXPECT_EQ(d2, original);
}

// ============================================================
// 错误路径
// ============================================================
TEST_F(AesEncryptTest, DecryptInvalidDataFails) {
    std::string garbage = "not valid data";
    std::string plaintext;
    EXPECT_FALSE(aes_.Decrypt(garbage, plaintext));
}

TEST_F(AesEncryptTest, DecryptTruncatedDataFails) {
    std::string original = "test data";
    std::string encrypted;
    ASSERT_TRUE(aes_.Encrypt(original, encrypted));

    // Base64 数据截断
    encrypted.resize(encrypted.size() / 2);
    std::string plaintext;
    EXPECT_FALSE(aes_.Decrypt(encrypted, plaintext));
}

// ============================================================
// 单例
// ============================================================
TEST_F(AesEncryptTest, SingletonReturnsSameInstance) {
    AesEncrypt& a = AesEncrypt::getInstance();
    AesEncrypt& b = AesEncrypt::getInstance();
    EXPECT_EQ(&a, &b);
}

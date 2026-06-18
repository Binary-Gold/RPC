#include "encrypt/aes_encrypt.hpp"
#include "log_manager.hpp"

#include <aes.h>
#include <modes.h>
#include <filters.h>
#include <osrng.h>
#include <base64.h>

namespace cookrpc
{

struct AesEncrypt::Imp {
    CryptoPP::SecByteBlock key_;  // 16 bytes
    CryptoPP::AutoSeededRandomPool rng_;

    Imp() {
        key_.resize(CryptoPP::AES::DEFAULT_KEYLENGTH);
        memset(key_.data(), 0, key_.size());
        memcpy(key_.data(), "CookRPC_2026_Key!", 16);
    }
};

AesEncrypt::AesEncrypt() : imp_(std::make_unique<Imp>()) {}
AesEncrypt::~AesEncrypt() = default;

bool AesEncrypt::Encrypt(const std::string& data, std::string& ciphertext) {
    try {
        if (data.empty()) {
            LOG_ERROR("Input data is empty");
            return false;
        }

        // 随机 IV，确保相同明文产生不同密文
        CryptoPP::SecByteBlock iv(CryptoPP::AES::BLOCKSIZE);
        imp_->rng_.GenerateBlock(iv.data(), iv.size());

        std::string encrypted;
        CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption enc;
        enc.SetKeyWithIV(imp_->key_.data(), imp_->key_.size(), iv.data());
        CryptoPP::StringSource ss(data, true,
            new CryptoPP::StreamTransformationFilter(enc,
                new CryptoPP::StringSink(encrypted)));

        // IV 拼接在密文前面一起发送
        std::string iv_str(reinterpret_cast<const char*>(iv.data()), iv.size());
        std::string combined = iv_str + encrypted;

        CryptoPP::StringSource(combined, true,
            new CryptoPP::Base64Encoder(new CryptoPP::StringSink(ciphertext), false));
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Encryption failed: {}", e.what());
        return false;
    }
}

bool AesEncrypt::Decrypt(const std::string& encrypted_data, std::string& plaintext) {
    try {
        std::string decoded;
        CryptoPP::StringSource(encrypted_data, true,
            new CryptoPP::Base64Decoder(new CryptoPP::StringSink(decoded)));

        if (decoded.size() <= CryptoPP::AES::BLOCKSIZE) {
            LOG_ERROR("Decoded data too short: {} bytes", decoded.size());
            return false;
        }

        // 从密文头取出 IV
        CryptoPP::SecByteBlock iv(CryptoPP::AES::BLOCKSIZE);
        memcpy(iv.data(), decoded.data(), iv.size());

        std::string decrypted;
        CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption dec;
        dec.SetKeyWithIV(imp_->key_.data(), imp_->key_.size(), iv.data());
        CryptoPP::StringSource ss(
            reinterpret_cast<const CryptoPP::byte*>(decoded.data()) + CryptoPP::AES::BLOCKSIZE,
            decoded.size() - CryptoPP::AES::BLOCKSIZE,
            true,
            new CryptoPP::StreamTransformationFilter(dec,
                new CryptoPP::StringSink(decrypted)));

        plaintext = std::move(decrypted);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Decryption failed: {}", e.what());
        return false;
    }
}

} // namespace cookrpc

#pragma once
#include <string>
#include <memory>

namespace cookrpc
{
    class AesEncrypt
    {
    public:
        static AesEncrypt &getInstance() {
            static AesEncrypt instance;
            return instance;
        }

        bool Encrypt(const std::string &data, std::string &ciphertext);
        bool Decrypt(const std::string &encrypted_data, std::string &plaintext);

        AesEncrypt(const AesEncrypt &) = delete;
        AesEncrypt &operator=(const AesEncrypt &) = delete;

    private:
        AesEncrypt();
        ~AesEncrypt();

        struct Imp;
        std::unique_ptr<Imp> imp_;
    };

} // namespace cookrpc

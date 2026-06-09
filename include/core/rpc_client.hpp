#pragma once
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <future>
#include <sys/socket.h>
#include <netinet/in.h>
#include "proto/rpc_envelope.pb.h"
#include "network/connection.hpp"
#include "protocol/rpc_protocol.hpp"
#include "compress_data/zstd_compress.hpp"
#include "encrypt/aes_encrypt.hpp"
#include "serializer/protobuf_serializer.h"
#include "log_manager.hpp"
#include "core/error_code.hpp"

namespace cookrpc
{

    class RpcClient
    {
    public:
        using ResponseCallback = std::function<void(const std::string&, bool)>;

        explicit RpcClient(const std::string& config_path = "config/rpc_client.json");
        ~RpcClient();

        RpcClient(const RpcClient&) = delete;
        RpcClient& operator=(const RpcClient&) = delete;

        bool loadConfig(const std::string& conf_path);
        bool Connect();
        std::string GetServerAddress(const std::string& zk_namespace) const;
        bool initSocket(int& fd);
        bool validateServerInfo(const std::string& ip, int port, int fd);
        bool tryConnect(int fd, const struct sockaddr_in& server_addr, int retry_count);
        void Disconnect();
        bool IsConnected() const;

        template <typename Response, typename Request>
        bool Call(const std::string& service_name,
                  const std::string& method_name,
                  const Request& request,
                  Response& response)
        {
            try {
                std::lock_guard<std::mutex> lock(GetMutex());
                if (!checkConnection()) {
                    LOG_ERROR("Connection check failed");
                    return false;
                }
                std::string encrypted_data;
                if (!prepareAndSerializeRequest(service_name, method_name, request, encrypted_data)) {
                    return false;
                }
                if (!sendAndReceiveResponse(encrypted_data)) {
                    return false;
                }
                return processResponse(response);
            } catch (const std::exception& e) {
                LOG_ERROR("RPC call exception: {}", e.what());
                return false;
            }
        }

        template <typename Response, typename Request>
        std::future<Response> AsyncCall(
            const std::string& service_name,
            const std::string& method_name,
            const Request& request)
        {
            return std::async(std::launch::async,
                              [this, service_name, method_name, request]() {
                                  Response response;
                                  if (this->Call<Response, Request>(service_name, method_name, request, response)) {
                                      return response;
                                  }
                                  throw std::runtime_error("RPC call failed");
                              });
        }

    private:
        bool Reconnect();
        uint64_t GenerateSequenceId();
        bool checkConnection();
        bool sendAndReceiveResponse(const std::string& encrypted_data);
        std::shared_ptr<Connection>& GetConnection();
        int GetTimeoutMs() const;
        std::mutex& GetMutex();

        template <typename Request>
        bool prepareAndSerializeRequest(
            const std::string& service_name,
            const std::string& method_name,
            const Request& request,
            std::string& encrypted_data)
        {
            std::string user_serialized_data;
            if (!ProtobufSerializer::Serialize(request, user_serialized_data)) {
                LOG_ERROR("Failed to serialize request");
                return false;
            }

            RpcRequest rpc_request;
            rpc_request.setServiceName(service_name);
            rpc_request.setMethodName(method_name);
            rpc_request.setPayload(user_serialized_data);
            rpc_request.setSequenceId(GenerateSequenceId());

            std::string rpc_serialized_data;
            if (!rpc_request.Serialize(rpc_serialized_data)) {
                LOG_ERROR("RpcRequest serialize failed");
                return false;
            }

            std::string compressed_data;
            if (!ZstdCompress::getInstance().CompressString(rpc_serialized_data, compressed_data)) {
                LOG_ERROR("RpcRequest compress failed");
                return false;
            }

            if (!AesEncrypt::getInstance().Encrypt(compressed_data, encrypted_data)) {
                LOG_ERROR("RpcRequest encrypt failed");
                return false;
            }

            // Prepend RpcHeader
            RpcHeader header;
            header.magic = RpcHeader::MAGIC;
            header.message_length = encrypted_data.size();
            header.sequence_id = rpc_request.getSequenceId();
            std::string frame;
            frame.resize(sizeof(header) + encrypted_data.size());
            std::memcpy(&frame[0], &header, sizeof(header));
            std::memcpy(&frame[sizeof(header)], encrypted_data.data(), encrypted_data.size());
            encrypted_data = std::move(frame);
            return true;
        }

        template <typename Response>
        bool processResponse(Response& response)
        {
            if (!GetConnection()->ReadWithTimeout(GetTimeoutMs())) {
                LOG_ERROR("Failed to read response data within timeout");
                return false;
            }

            std::string raw_response_data = GetConnection()->GetReadBufferData();
            if (raw_response_data.size() < sizeof(RpcHeader)) {
                LOG_ERROR("Response too small");
                return false;
            }

            RpcHeader header;
            std::memcpy(&header, raw_response_data.data(), sizeof(header));
            if (header.magic != RpcHeader::MAGIC) {
                LOG_ERROR("Bad response magic");
                return false;
            }

            std::string encrypted_body(
                raw_response_data.begin() + sizeof(header),
                raw_response_data.begin() + sizeof(header) + header.message_length);

            std::string decrypted_data;
            if (!AesEncrypt::getInstance().Decrypt(encrypted_body, decrypted_data)) {
                LOG_ERROR("Failed to decrypt response data");
                return false;
            }

            std::string decompressed_data;
            if (!ZstdCompress::getInstance().DecompressString(decrypted_data, decompressed_data)) {
                LOG_ERROR("Failed to decompress response data");
                return false;
            }

            RpcResponse rpc_response;
            if (!rpc_response.Deserialize(decompressed_data)) {
                LOG_ERROR("Failed to deserialize RPC response");
                return false;
            }

            if (rpc_response.getErrorCode() != static_cast<uint32_t>(ErrorCode::SUCCESS)) {
                LOG_ERROR("RPC call failed: {} (code: {})",
                          rpc_response.getErrorMessage(), rpc_response.getErrorCode());
                return false;
            }

            std::string result_data = rpc_response.getResultData();
            if (!ProtobufSerializer::Deserialize(result_data, response)) {
                LOG_ERROR("Failed to deserialize user response data");
                return false;
            }

            CLIENT_INFO("Successfully processed response");
            return true;
        }

        struct Imp;
        std::unique_ptr<Imp> imp_;
    };

} // namespace cookrpc

#pragma once

#include <memory>
#include <string>
#include <filesystem>
#include <iostream>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "fmt/format.h"

/**
 * 基础日志宏定义
 * 提供不同级别的日志记录功能:
 * TRACE: 最详细的日志级别,用于追踪程序执行流程
 * DEBUG: 调试信息,用于开发调试
 * INFO: 一般信息,记录程序正常运行状态
 * WARN: 警告信息,表示潜在的问题
 * ERROR: 错误信息,表示程序运行出错
 * FATAL: 致命错误,程序无法继续运行
 */
#define LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define LOG_FATAL(...) SPDLOG_CRITICAL(__VA_ARGS__)

/**
 * 客户端专用日志宏定义
 * 在基础日志级别上添加[CLIENT]前缀,用于区分客户端日志
 * 便于在日志中快速识别客户端相关的信息
 */
#define CLIENT_INFO(...) SPDLOG_INFO("[CLIENT] " __VA_ARGS__)
#define CLIENT_DEBUG(...) SPDLOG_DEBUG("[CLIENT] " __VA_ARGS__)
#define CLIENT_WARN(...) SPDLOG_WARN("[CLIENT] " __VA_ARGS__)
#define CLIENT_ERROR(...) SPDLOG_ERROR("[CLIENT] " __VA_ARGS__)
#define CLIENT_FATAL(...) SPDLOG_CRITICAL("[CLIENT] " __VA_ARGS__)

/**
 * 线程ID格式化器
 * 用于在日志中格式化输出线程ID
 */
template <>
struct fmt::formatter<std::thread::id> : fmt::formatter<std::string>
{
    template <typename FormatContext>
    auto format(const std::thread::id &id, FormatContext &ctx) const
    {
        std::ostringstream oss;
        oss << id;
        return fmt::formatter<std::string>::format(oss.str(), ctx);
    }
};

/**
 * Logger
 * 日志管理器类，单例模式实现
 *
 * 主要功能：
 * 1. 初始化日志系统
 * 2. 配置日志输出（控制台和文件）
 * 3. 管理日志格式和级别
 * 4. 提供全局访问点
 */
 class Logger final
{
public:
    /**
     *  获取Logger单例实例
     *  Logger& 返回Logger实例的引用
     */
    static Logger &GetInstance()
    {
        static Logger instance;
        return instance;
    }

    /**
     *  初始化日志系统
     *  log_dir 日志文件存储目录，默认为"../logs"
     *  bool 初始化是否成功
     *
     * 初始化过程：
     * 1. 创建日志目录
     * 2. 设置控制台输出（彩色）
     * 3. 设置每日日志文件
     * 4. 配置日志格式和级别
     * 5. 设置为默认logger
     */
    bool Init(const std::string &log_dir = "../logs")
    {
        try {
            // 创建日志目录
            std::filesystem::path log_path(log_dir);
            if (!std::filesystem::exists(log_path))
            {
                if (!std::filesystem::create_directories(log_path))
                {
                    std::cerr << "Failed to create log directory: " << log_dir << std::endl;
                    return false;
                }
            }

            // 创建控制台和文件日志记录器
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            if (!console_sink)
            {
                std::cerr << "Failed to create console sink" << std::endl;
                return false;
            }

            std::string log_file = log_dir + "/cookrpc.log";
            auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(log_file, 0, 0);
            if (!file_sink)
            {
                std::cerr << "Failed to create file sink: " << log_file << std::endl;
                return false;
            }

            std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
            logger_ = std::make_shared<spdlog::logger>("main", sinks.begin(), sinks.end());
            if (!logger_)
            {
                std::cerr << "Failed to create logger" << std::endl;
                return false;
            }
                // 设置日志格式
            logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%%^%l%%$] [%t] %v");
            logger_->set_level(spdlog::level::trace);

            // 设置为默认logger
            spdlog::set_default_logger(logger_);

            // 测试日志是否可写
            try
            {
                // LOG_INFO("Logger initialized successfully");
            }
            catch (const std::exception &e) {
                std::cerr << "Failed to write test log: " << e.what() << std::endl;
                return false;
            }

            return true;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Logger initialization failed: " << e.what() << std::endl;
            return false;
        }
    }

    // 禁止拷贝构造和赋值操作，确保单例模式
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

private:
    Logger() = default;  // 私有构造函数
    ~Logger() = default; // 私有析构函数
    std::shared_ptr<spdlog::logger> logger_;  // spdlog日志记录器实例
};
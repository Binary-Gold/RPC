# CookRPC 从零到压测 — 教程总纲

本系列文档面向有一定 C++ 基础、想理解 RPC 框架底层原理的读者。
读完你应该能：复述一次 RPC 调用的完整路径，说出每个模块的职责，并理解性能数据背后的原因。

## 目录

| 章节 | 文件 | 核心问题 |
|------|------|----------|
| 0 | 本文 | 大局观：六层架构是怎样的 |
| 1 | [Part1-请求旅途.md](Part1-请求旅途.md) | 一个 Call() 经过了什么 |
| 2 | [Part2-网络层.md](Part2-网络层.md) | epoll 怎么盯住几千个连接 |
| 3 | [Part3-协议层.md](Part3-协议层.md) | 字节流怎么变对象，再加压缩加密 |
| 4 | [Part4-调度层.md](Part4-调度层.md) | 线程池怎么接活，服务怎么路由 |
| 5 | [Part5-注册发现.md](Part5-注册发现.md) | ZK 怎么帮客户端找到服务端 |
| 6 | [Part6-压测报告.md](Part6-压测报告.md) | 5万QPS意味着什么 |

## 六层架构一图概括

```
┌──────────────────────────────────────────────┐
│  用户代码: client.Call("RpcService","Echo",...) │
├──────────────────────────────────────────────┤
│  5.注册发现层: ZK查服务 → LoadBalancer选实例   │
│  4.调度层:    ThreadPool接活 → ServiceManager路由 │
│  3.协议层:    Proto→zstd→AES→RpcHeader帧切分  │
│  2.网络层:    epoll事件循环 → Connection收发    │
│  1.基础设施:   Logger + gtest + CMake          │
└──────────────────────────────────────────────┘
```

## 关键术语速查

| 术语 | 含义 | 在项目中 |
|------|------|----------|
| RPC | 像调本地函数一样调远程函数 | 全项目 |
| epoll | Linux内核的"盯梢者"，一个线程盯N个fd | message_cycle.cpp |
| 事件循环 | while(running){epoll_wait;处理事件} | Loop() |
| 帧切分 | 在字节流中找RpcHeader→按长度切出完整消息 | ProcessMessage() |
| 粘包/半包 | TCP不保边界，多条消息粘在一起或一条拆两半 | ProcessMessage解决 |
| 序列化 | 对象→字节流 | ProtobufSerializer |
| 线程池 | 预创建N条线程，任务来了挑一条空闲的干 | ThreadPool |
| 异步 | 不阻塞当前线程，活儿丢给别人干 | HandleRpcRequestAsync |
| 服务发现 | 客户端问"谁提供这个服务" | ZkConnHandler |
| 负载均衡 | 多个服务实例中挑一个 | LoadBalancer |
| Pimpl | 头文件只暴露声明，实现藏在.cpp | 全项目 |
| RAII | 资源获取即初始化，构造函数拿资源析构还 | Connection, ServiceRegistry |

## 项目文件地图

```
include/
├── core/          rpc_client.hpp (客户端入口), rpc_service.hpp (业务基类)
├── protocol/      rpc_protocol.hpp (RpcHeader定义)
├── network/       connection.hpp, message_cycle.hpp (事件循环)
├── serializer/    protobuf_serializer.h (序列化模板)
├── encrypt/       aes_encrypt.hpp (对称加密)
├── compress_data/ zstd_compress.hpp (zstd压缩)
├── service/       service_manager.hpp (服务注册+调度)
├── load_balancer/ zk_conn_handler.hpp (ZK连接), load_balancer.hpp (LB基类)
├── thread_pool/   thread_pool.hpp (弹性线程池)
└── load_config/   rpc_client_config.hpp, rpc_server_config.hpp

src/   (与 include/ 镜像实现)
test/  benchmark.cpp (压测),  *_test.cpp (单元测试)
```

## 推荐阅读顺序

1. 先读 Part1 — 跟一次请求走完七步，建立全局感
2. 读 Part2 — 理解事件循环和 epoll，这是框架的心脏
3. 读 Part3 — 理解协议层，数据怎么在路上走
4. 读 Part4/5 — 调度和发现，微服务化必备
5. 读 Part6 — 看压测结果，理解性能数据

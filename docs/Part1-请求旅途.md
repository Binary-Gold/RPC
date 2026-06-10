# Part1：一次 RPC 请求的完整旅途

从 `benchmark.cpp` 的一行代码开始，追踪它经过的每一层。

## 起点：`test/benchmark.cpp`

```cpp
client.Call<EchoResponse, EchoRequest>("RpcService", "Echo", req, resp);
```

一行。和调本地函数一样——这就是 RPC 的全部意义。但这一行经过了七道工序：

## 七道工序

```
Client 端:                           Server 端:
① Serialize (proto)                 ⑤ ProcessMessage (拆帧)
② Compress (zstd)                   ⑥ 解密→解压→反序列化
③ Encrypt  (AES)                    ⑦ ServiceManager → 业务逻辑
④ prepend RpcHeader → send          ⑧ 回包 (④③②①反过来)
```

## 第一站：序列化 (`rpc_client.hpp:prepareAndSerializeRequest`)

```cpp
// 1. 用户数据 → protobuf
ProtobufSerializer::Serialize(request, user_serialized_data);

// 2. 包装成 RPC 协议包
RpcRequest rpc_request;
rpc_request.setServiceName("RpcService");
rpc_request.setMethodName("Echo");
rpc_request.setPayload(user_serialized_data);
rpc_request.Serialize(rpc_serialized_data);

// 3. 压缩
ZstdCompress::getInstance().CompressString(rpc_serialized_data, compressed);

// 4. 加密
AesEncrypt::getInstance().Encrypt(compressed, encrypted);

// 5. 加帧头
RpcHeader header;
header.magic = 0x12345678;
header.message_length = encrypted.size();
header.sequence_id = seq_id;

// 最终格式：[RpcHeader(12字节)][加密体]
frame = [header] + encrypted;
```

**关键点**：RpcHeader 是**明文**的。必须先知道长度才能切出要解密的密文——头加密了就死循环。

## 第二站：发出去+读回来 (`rpc_client.hpp:sendAndReceiveResponse+processResponse`)

```cpp
// 发
connection_->Write(frame);           // write(fd, ...)

// 读
connection_->ReadWithTimeout(3s);    // select+read，超时就失败
string raw = connection_->GetReadBufferData();  // 取出，清空buffer

// 拆头
RpcHeader header;
memcpy(&header, raw.data(), 12);
if (header.magic != 0x12345678) error;

// 解密→解压→反序列化
string body = raw.substr(12, header.message_length);
decrypt → decompress → Deserialize → EchoResponse
```

## 第三站：服务端接客 (`message_cycle.cpp`)

```cpp
// 事件循环——永远在跑
while (running_) {
    int n = epoll_wait(epoll_fd, events, 1024, 1000);

    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;

        if (fd 是大门) {
            accept(fd) → 新 client_fd → 注册进 epoll;
        } else {
            conn->Read();        // 倒空 socket，倒进 buffer
            conn->ProcessMessage(); // 拆帧→解密→解压→反序列化
        }
    }
}
```

**epoll 是门房**——一个人盯着所有门，谁发出声谁来处理。这是非阻塞事件循环：没数据不占 CPU，有数据秒响应。

## 第四站：业务处理 (`service_manager.hpp`)

```cpp
// 请求到了 → 丢给线程池（异步）
HandleRpcRequestAsync(conn, request)
  → ThreadPool.Enqueue(任务)

// 线程池线程执行
HandleRpcRequestSync(conn, request)
  → auto service = services_["RpcService"];
  → service->HandleRequest("Echo", payload, result);
```

**为什么异步**：epoll 线程不能阻塞。如果它卡在业务处理上，后面的 I/O 全停了。把计算任务丢给线程池，epoll 线程立刻回去盯 fd。

## 第五站：回包

完全对称——`Serialize → Compress → Encrypt → prepend Header → send`。

## 总结

一次 RPC 干了什么：**对象→proto→压缩→加密→套头→写socket→epoll唤醒→拆头→解密→解压→反序列化→业务处理→原路返回**。

`benchmark.cpp` 里的一条 `Call` ——你看到的是一行，它走过的是七层。

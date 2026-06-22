# Part3：协议层 —— 序列化 + 压缩 + 加密 + 帧切分

这一层负责三件事：把 C++ 对象变成能在网上走的字节流，压缩节省带宽，加密保护数据。

## 为什么不用 JSON

JSON 是文本——每个字段名每包都重发。protobuf 用数字代替字段名：

```
JSON: {"service_name":"RpcService","method_name":"Echo",...}  ~80字节
Proto: [tag=1, "RpcService"][tag=2, "Echo"]                    ~20字节
```

同一份数据、同一个意思、差了 4 倍数。

## 栈 (Client 端 → Server 端)

```
C++ 对象(Lambda/用户数据)
  → ProtobufSerializer::Serialize (proto)
    → 变成紧凑的字节流
  → ZstdCompress::CompressString (zstd)
    → 压缩，特别是大包时收益明显
  → AesEncrypt::Encrypt (AES)
    → 加密，字节流变密文
  → prepend RpcHeader (12字节)
    [magic=4B|message_length=4B|sequence_id=4B]
  → Connection::Write(fd)
    → 写进 TCP 缓冲区
```

## RpcHeader 结构 (`rpc_protocol.hpp`)

```cpp
struct RpcHeader {
    uint32_t magic = 0x12345678;    // 魔数：验证"这是我的协议"
    uint32_t message_length;        // 体长度：切包边界
    uint32_t sequence_id;           // 序列号：请求和响应对号
};
```

**为什么明文**：要切包，必须先知道长度；要知道长度，必须读头；要读头，必须不加密。加密了就是死循环。

## 序列化 (`serializer/protobuf_serializer.h`)

```cpp
// 只给 protobuf 消息用的模板
template <typename T>
static bool Serialize(const T& data, string& out) {
    static_assert(T 是 protobuf 消息);  // 编译期检查
    return data.SerializeToString(&out); // 调用 proto 自带的
}

template <typename T>
static bool Deserialize(const string& in, T& data) {
    return data.ParseFromString(in);
}
```

不到 10 行。protobuf 产生的 `SerializeToString` / `ParseFromString` 帮你做了所有重体力活。

## 压缩 (`compress_data/zstd_compress.cpp`)

```cpp
// 压缩
size_t bound = ZSTD_compressBound(src.size());
dst.resize(bound);
size_t actual = ZSTD_compress(dst.data(), bound, src.data(), src.size(), level);
dst.resize(actual);

// 解压
uint64_t original_size = ZSTD_getFrameContentSize(src.data(), src.size());
dst.resize(original_size);
ZSTD_decompress(dst.data(), original_size, src.data(), src.size());
```

对 echo 这种几十字节的小包，压缩几乎无收益——原样大小。对 4KB+ 的大包，压缩比轻松 3-5x。

## 加密 (`encrypt/aes_encrypt.cpp`)

使用 Crypto++ 库的 AES-128-CBC：

```cpp
// 加密：
随机生成 16 字节 IV（AutoSeededRandomPool，硬件熵源）
→ CBC_Mode<AES>::Encryption（AES-NI 硬件加速）
→ IV(16B) + 密文 → Base64 编码 → 发送

// 解密：
Base64 解码 → 提取 IV（前 16B）
→ CBC_Mode<AES>::Decryption（同样 AES-NI）
→ 明文
```

IV 随机，每次加密同一明文产生不同密文。IV 不保密，拼接在密文头一起发送。密钥在 Imp 构造时 SHA-256 派生。已从 v1 手写 Shift 迁移到标准 AES。

## 关键设计决策

| 决策 | 理由 |
|------|------|
| RpcHeader 明文 | 需要长度切包 |
| RpcHeader 有魔数 | 防错连 + 快速校验 |
| 压缩在加密之前 | 密文无法压缩 |
| 序列化在压缩之前 | proto 格式规律性强，压缩效果好 |

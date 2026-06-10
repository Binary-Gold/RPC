# Part2：网络层 —— epoll + 非阻塞 IO + 连接管理

框架的心跳。没有它，上面所有序列化、业务逻辑都跑不动。

## 核心问题

"1万个客户端同时连你，你只有1条线程，怎么办？"

老方法：**1连接→1线程**。开1万个线程，每个8MB栈→80GB内存直接爆。CPU全花在线程切换上，谁都不干活。

epoll 的答案：**1条线程盯1万个 fd**。内核帮你挂起，谁有数据叫谁。

## epoll 全流程 (`message_cycle.cpp`)

```cpp
// 初始化：造一个 epoll 管理器
epoll_fd = epoll_create1(0);

// 加入监听：
epoll_ctl(epoll_fd, ADD, server_fd, EPOLLIN);  // "盯大门"
epoll_ctl(epoll_fd, ADD, client_fd, EPOLLIN|EPOLLET); // "盯对讲机"

// 事件循环：
while (running) {
    int n = epoll_wait(epoll_fd, events, 1024, 1000);
    // 内核把线程挂起。没事件：睡1秒再查
    // 有事件：秒醒，events[0..n-1] 就是有事的fd

    for (int i=0; i<n; i++) {
        int fd = events[i].data.fd;

        if (fd 是 listen_fd) {
            accept(fd) → 新client_fd → epoll_ctl(ADD);
        } else {
            conn = ConnectionManager.Find(fd);
            conn->Read() → 倒空socket;
            conn->ProcessMessage() → 拆帧→解密→解压→反序列化;
        }
    }
}
```

## 三种 fd

| fd | 类型 | 作用 | 数量 |
|----|------|------|------|
| epoll_fd | 管理器 | 盯所有 fd | 1 |
| server_fd (listen) | 大门 | accept 分配新 client_fd | 1 |
| client_fd | 对讲机 | 跟这一个客户端收发 | N |

## 水平触发 vs 边缘触发

```
水平触发 (LT): 缓冲区有数据 → epoll_wait 就返回
              没读完 → 下次还返回（有保底，不怕忘读）

边缘触发 (ET): 缓冲区"从空变有"→ epoll_wait 返回一次
              没读完 → 不再通知！必须一次性读到 EAGAIN
```

你的框架：client_fd 用 ET（高效，必须倒空），server_fd 用 LT（低频，安全）。

## 帧切分：解决粘包 (`connection.cpp:ProcessMessage`)

TCP 是字节流，没有消息边界。连续发两条：
```
[包1: 50字节][包2: 30字节] → TCP 里就一个 80 字节的 stream
```

解法：每包前加 RpcHeader(12字节)：

```cpp
// ProcessMessage 的核心逻辑
while (read_buffer >= 12) {           // 够一个头
    RpcHeader h;
    memcpy(&h, buffer, 12);           // 读头
    if (h.magic != 0x12345678) error; // 校验魔数

    if (buffer < 12 + h.length) break; // 半包，等下次

    // 切出一帧
    string frame = buffer[12..12+h.length];
    解密→解压→反序列化→HandleMessage;

    buffer.erase(0, 12+h.length);     // 切掉，继续下一帧
}
```

## 非阻塞 IO (`connection.cpp:Read`)

```cpp
while (true) {
    int n = read(fd, buf, 4096);
    if (n < 0) {
        if (errno == EAGAIN) return;  // 读空了，正常的
        else error;                    // 真出错了
    }
    if (n == 0) disconnect;           // 对端关了
    buffer.insert(buf, buf+n);        // 存起来
}
```

非阻塞模式下，`read` 不等人——有数据就返回，没数据返回 -1 + EAGAIN。循环读直到 EAGAIN 表示倒空——边缘触发下的标准做法。

## 连接生命周期

```
accept(fd) → new Connection(fd)
  → 设非阻塞 + TCP_NODELAY + 回调
  → 加入 epoll + ConnectionManager

HTTP请求到达:
  → epoll_wait 返回
  → ConnectionManager.Find(fd)
  → conn->Read() → conn->ProcessMessage()
  → 回调 → MessageCycle::HandleRpcRequestAsync
  → 线程池：ServiceManager::HandleRpcRequestSync
  → conn->Write(response)

对端关闭或出错:
  → close(fd)
  → epoll_ctl(DEL)
  → ConnectionManager.Remove(fd)
```

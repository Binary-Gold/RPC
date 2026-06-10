# Part4：调度层 —— 线程池 + 服务管理

网络请求到了——谁来处理？这一层的答案：线程池扛计算，ServiceManager 路由到具体业务。

## 线程池 (`thread_pool/thread_pool.cpp`)

### 为什么需要一个池

epoll 线程不能阻塞。如果它直接调业务逻辑（比如查数据库要 50ms），200 个请求要排队 200 × 50ms = 10 秒——后面的 I/O 全堵死。

解法：epoll 线程把活儿**丢给线程池**，自己立刻回去盯 fd。

```
epoll 线程:  收请求 → 丢池子 → 回去干活  (1μs)
线程池线程:  取任务 → 业务逻辑 → 回包      (50μs-50ms不等)
```

### 核心结构

```cpp
// 投递任务
pool.Enqueue(priority, [conn, request] {
    ServiceManager::HandleRpcRequestSync(conn, request);
});

// Enqueue 内部：
锁 → 队列.push_back(任务) → notify_one() → 解锁

// WorkerThread 内部：
while (true) {
    wait(有任务或stop);
    task = 队列.pop();
    解锁 → task.Execute() → 锁
}
```

**关键**：执行任务时**不持锁**——否则只有一个线程能干活。

### 优先级

```
HIGH(2)  → RPC 请求处理  ← 最重要的
NORMAL(1)
LOW(0)
```

队列是 `priority_queue`，HIGH 任务自动排前面。

### 弹性伸缩

```
初始:  core_threads 条线程
高峰期: 队列堆积 → 自动扩容到 max_threads
低峰期: 线程空转 keep_alive_time → 回收到 core_threads
```

### 优雅停机

```
state → SHUTTING_DOWN  (拒新任务，手头的干完)
  → 等当前任务结束
  → state → STOPPED
  → join 所有线程
```

## ServiceManager (`service/service_manager.hpp`)

### 服务注册

```cpp
// 把你的业务服务注册进来
ServiceManager::GetInstance().RegisterService(myService);

// 内部就是一个 map
services_["UserService"] = myService;
```

### 请求路由

```cpp
// 网络来的请求，异步走
HandleRpcRequestAsync(conn, request)
  → ThreadPool.Enqueue(HIGH, [=]{
        HandleRpcRequestSync(conn, request);
    });

// 框架内部调用，同步走
HandleRpcRequestSync(conn, request)
  → auto service = services_["RpcService"];
  → service->HandleRequest("Echo", payload, result);
```

### 为什么两个接口

```
HandleRpcRequestAsync  → epoll 线程用（不阻塞）
HandleRpcRequestSync   → 已在线程池里时用（避免双重入池）
```

池内调异步 = 自己的子任务等自己不释放 → 等不到 → 死等。池内只调同步。

## 业务服务的接口

```cpp
class Service {
    virtual string GetServiceName() = 0;
    virtual bool HandleRequest(method_name, args, result) = 0;
};
```

实现一个服务只需要十几行：
```cpp
class MyService : public Service {
    string GetServiceName() override { return "MyService"; }
    bool HandleRequest(string method, string args, string& result) override {
        if (method == "SayHello") {
            result = "Hello from server!";
            return true;
        }
        return false;
    }
};
```

业务逻辑和框架完全解耦。

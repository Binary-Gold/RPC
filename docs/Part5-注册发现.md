# Part5：注册与发现 —— ZooKeeper + 负载均衡

"调用 UserService.Login"——但 UserService 跑在哪台机器上？ZK 负责回答。

## 场景

```
Provider(服务提供方, 3台):
  10.0.0.1:8080  → 启动时往ZK写: /cookrpc/UserService/10.0.0.1:8080
  10.0.0.2:8080  → 同上
  10.0.0.3:8080  → 同上

Consumer(调用方):
  "我要调 UserService" → 问ZK → 返回 [.1, .2, .3]
  → LoadBalancer挑一台 → 10.0.0.2:8080 → TCP连接 → 发请求
```

## ZK 的数据模型 —— 一棵文件系统树

```
/cookrpc                     ← 你的项目根（持久节点）
  ├── /UserService            ← 服务名（持久节点）
  │   ├── 10.0.0.1:8080       ← 实例1（临时节点）
  │   ├── 10.0.0.2:8080       ← 实例2（临时节点）
  │   └── 10.0.0.3:8080       ← 实例3（临时节点）
  └── /OrderService           ← 另一个服务
```

## 临时节点 = 自动下线

```
Provider 启动 → zoo_create(EPHEMERAL, "/cookrpc/UserService/10.0.0.1:8080")
Provider 宕掉 → TCP断开 → ZK等session_expire → 自动删除节点
Consumer    → Watch通知 → 更新实例列表，移除 10.0.0.1
```

扩展实例同理——新节点出现，Watch 通知 Consumer 加上。

## 注册代码 (`zk_conn_handler.cpp`)

```cpp
// 服务端：RegisterServicesFromConfig
for (每个实例) {
    string path = "/cookrpc/" + service_name;
    EnsurePath_(path);                          // 确保父节点存在
    CreateNode_(path + "/" + address, data, ZOO_EPHEMERAL); // 临时节点
}

// 客户端：GetServices
auto instances = service_registry_->DiscoverServices("cookrpc_service");
// 返回 ["127.0.0.1:8989"]

// 再选一个
string server = LoadBalancer::selectServer(instances);
// 返回 "127.0.0.1:8989"
```

## 负载均衡 (`load_balancer/`)

三种策略：

| 策略 | 文件 | 做法 | 使用场景 |
|------|------|------|----------|
| 轮询(Round) | round.hpp | atomic计数器, fetch_add % N | 默认、通用 |
| 随机(Random) | random.hpp | mt19937+均匀分布 | 压测 |
| 加权(Weighted) | weighted.hpp | 权重区间累计 | 异构机器 |

```cpp
// 轮询核心：线程安全的一条原子操作
size_t idx = index_.fetch_add(1, relaxed) % n;
return instances[idx];
```

## ServiceRegistry (`services_registry.cpp`)

直接封装 ZK C API——初始化连接、创建节点、watch 回调都在这里。

```cpp
ServiceRegistry registry("127.0.0.1:2181");

// 注册
registry.RegisterService("UserService", "10.0.0.1:8080");

// 发现
auto instances = registry.DiscoverServices("UserService");
// → ["10.0.0.1:8080", "10.0.0.2:8080", "10.0.0.3:8080"]
```

## 全链路时序

```
Provider 启动:
  1. ZkConnHandler::InitZkConnHandler(config)
     → ServiceRegistry("127.0.0.1:2181") → zookeeper_init
  2. RegisterServicesFromConfig(servers.json)
     → 逐个创建 ZOO_EPHEMERAL 节点
  3. MessageCycle::Loop() → 进入事件循环

Consumer 调用:
  1. ZkConnHandler::InitZkConnHandler(config)
  2. GetServices("cookrpc_service")
     → DiscoverServices → LB选实例 → "127.0.0.1:8989"
  3. TCP连接 → RpcClient::Call → 发请求
```

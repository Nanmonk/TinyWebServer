# TinyWebServer 运行与测试指南

## 环境要求

| 依赖 | 版本要求 |
|------|----------|
| OS | Linux（Ubuntu 16.04+ 或同等发行版） |
| 编译器 | g++ (支持 C++11) |
| MySQL | 5.7+ |
| 构建工具 | CMake 3.14+ |
| 测试框架 | Google Test 1.10+ |
| 压测工具 | webbench（项目内置于 `test_pressure/webbench-1.5/`） |

---

## 第一步：配置 MySQL 数据库

登录 MySQL 并执行以下 SQL：

```sql
-- 创建数据库
CREATE DATABASE qgydb;

-- 创建用户表
USE qgydb;
CREATE TABLE user(
    username char(50) NULL,
    passwd char(50) NULL
) ENGINE=InnoDB;

-- 插入测试账号
INSERT INTO user(username, passwd) VALUES('admin', '123456');
```

确认 `main.cpp` 中的连接信息与数据库一致（默认值如下，如有不同请修改）：

```cpp
string user = "root";
string passwd = "root";
string databasename = "qgydb";
```

---

## 第二步：编译（CMake）

项目使用 CMake 构建，推荐 out-of-source 构建到 `build/` 目录。

```bash
# 配置（Debug 模式，自动生成 compile_commands.json）
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# 编译
cmake --build build -j$(nproc)

# 清理
rm -rf build/
```

编译成功后可执行文件位于 `build/server`。  
编译过程中出现的 `-Wreturn-type` 警告为已知问题（见 Issue #1），不影响运行。

### compile_commands.json（供 clangd / IDE 使用）

CMake 配置时自动生成于 `build/compile_commands.json`。项目根目录已建立软链接：

```bash
compile_commands.json -> build/compile_commands.json
```

clangd、clang-tidy 等 LSP 工具开箱即用。

---

## 第三步：单元测试（TDD）

测试文件位于 `test/` 目录，覆盖三个核心模块：

| 测试文件 | 覆盖模块 | 用例数 |
|----------|----------|--------|
| `test_block_queue.cpp` | 异步日志队列 `block_queue<T>` | 11 |
| `test_http_parser.cpp` | HTTP 状态机解析器 `http_conn` | 15 |
| `test_timer.cpp` | 升序定时器链表 `sort_timer_lst` | 10 |

### 编译并运行全部测试

```bash
# 构建（若已执行第二步则无需重复）
cmake -B build && cmake --build build -j$(nproc)

# 运行全部测试
ctest --test-dir build

# 带详细输出（显示每条用例结果）
ctest --test-dir build -V

# 只运行某一模块
ctest --test-dir build -R test_block_queue
```

### 测试结果（基准）

```
100% tests passed, 0 tests failed out of 39
Total Test time (real) = 0.14 sec

The following tests did not run (Disabled):
  BlockQueueTest.Front_ReturnsOldestElement_HasBug
  BlockQueueTest.TimeoutDuration_NanosecondBug
```

2 个 `DISABLED_` 测试用于记录源码中**已确认但暂未修复的 Bug**，不计入通过率：

| 禁用测试 | 对应 Bug | 涉及文件 |
|----------|----------|----------|
| `Front_ReturnsOldestElement_HasBug` | `block_queue::front()` 读 `m_array[m_front]`，但 `m_front` 初始为 `-1`，导致越界读取而非返回队首元素 | `log/block_queue.h` |
| `TimeoutDuration_NanosecondBug` | `pop()` 超时计算 `tv_nsec = ms * 1000`（微秒），应为 `* 1000000`（纳秒），实际超时时长仅为预期的 1/1000 | `log/block_queue.h:179` |

> 修复上述 Bug 后，将对应测试的 `DISABLED_` 前缀去掉即可自动纳入 CI。

---

## 第四步：启动服务器

### 默认启动

```bash
./build/server
# 默认监听 9006 端口
```

### 带参数启动

```bash
./build/server [-p port] [-l LOGWrite] [-m TRIGMode] [-o OPT_LINGER] [-s sql_num] [-t thread_num] [-c close_log] [-a actor_model]
```

| 参数 | 说明 | 可选值 | 默认值 |
|------|------|--------|--------|
| `-p` | 监听端口 | 任意合法端口 | `9006` |
| `-l` | 日志写入方式 | `0` 同步写入 / `1` 异步写入 | `0` |
| `-m` | listenfd + connfd 触发模式 | `0` LT+LT / `1` LT+ET / `2` ET+LT / `3` ET+ET | `0` |
| `-o` | 优雅关闭连接 | `0` 关闭 / `1` 开启 | `0` |
| `-s` | 数据库连接池大小 | 正整数 | `8` |
| `-t` | 线程池线程数 | 正整数 | `8` |
| `-c` | 日志开关 | `0` 开启日志 / `1` 关闭日志 | `0` |
| `-a` | 并发模型 | `0` Proactor / `1` Reactor | `0` |

**示例：**

```bash
# Reactor 模型 + ET+ET + 异步日志 + 优雅关闭 + 10 线程/连接
./build/server -p 9007 -l 1 -m 3 -o 1 -s 10 -t 10 -c 0 -a 1
```

---

## 第五步：浏览器功能测试

服务器启动后，在浏览器中访问：

```
http://127.0.0.1:9006
```

可测试以下功能：

| 功能 | 说明 |
|------|------|
| 用户注册 | 访问 `/register.html`，填写用户名和密码 |
| 用户登录 | 访问 `/log.html`，使用已注册账号登录 |
| 图片请求 | 访问 `/picture.html`，验证静态文件服务 |
| 视频请求 | 访问 `/video.html`，验证大文件传输 |
| 欢迎页面 | 登录成功后跳转至 `/welcome.html` |

静态资源文件均位于 `root/` 目录下。

---

## 第六步：压力测试（Webbench）

### 编译 webbench

```bash
cd test_pressure/webbench-1.5
make
```

### 执行压测

关闭日志可显著提升性能，建议压测时使用 `-c 1`：

```bash
# 启动服务器（关闭日志）
./build/server -c 1 &

# 压测：10500 并发连接，持续 5 秒
./test_pressure/webbench-1.5/webbench -c 10500 -t 5 http://127.0.0.1:9006/
```

### 参考性能指标

以下数据在关闭日志后测得：

| 模型 | 触发模式 | QPS |
|------|----------|-----|
| Proactor | LT + LT | ~93,251 |
| Proactor | LT + ET | ~97,459 |
| Proactor | ET + LT | ~80,498 |
| Proactor | ET + ET | ~92,167 |
| Reactor | LT + ET | ~69,175 |

---

## 常见问题

**Q：服务器启动时报 MySQL 连接失败**

检查 MySQL 是否运行，并确认 `main.cpp` 中的用户名、密码、库名与实际配置一致。

```bash
systemctl status mysql   # 或 mysqld
```

**Q：端口被占用**

```bash
# 查看占用端口的进程
ss -tlnp | grep 9006
# 换用其他端口启动
./server -p 9008
```

**Q：webbench 提示命令找不到**

进入 `test_pressure/webbench-1.5/` 目录重新编译：

```bash
cd test_pressure/webbench-1.5
make clean && make
```

**Q：编译报 `lmysqlclient` 找不到**

安装 MySQL 开发库：

```bash
# Ubuntu/Debian
sudo apt install libmysqlclient-dev

# Arch Linux
sudo pacman -S mariadb-libs
```

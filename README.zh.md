# NBIO

[English](README.md)

NBIO 是面向 Linux 的 C++20 异步 I/O 库，以协程、事件驱动调度器和通道式 I/O 为核心。

## 特性与设计

- **协程运行时：**惰性求值的 `Task<T>` 协程、对称转移、任务取消，以及并发组合器 `WhenAll` / `WhenAny`。
- **可选 I/O 后端：**默认使用 reactor 模型的 `epoll`。可选的 `io_uring` 后端采用完成事件驱动路径。两种后端可在同一构建中启用，并在初始化运行时选择多路复用器。
- **通道式 I/O：**异步 TCP 接入、连接、发送和接收，以及文件读写 API，均与多路复用器和协程调度器集成。
- **运行时服务：**基于定时器的休眠与截止时间、用于退出处理的信号等待、跨线程通知，以及基于 eventfd 的条件变量。
- **可选 RDMA：**启用构建选项并安装系统 RDMA 开发库后，可使用 RDMA 连接和数据传输通道。

I/O 操作由通道排队；多路复用器报告就绪或完成事件后，相应协程恢复执行。`epoll` 路径采用 reactor 模型，`io_uring` 路径采用 proactor 模型。`nbio::async::Run` 驱动调度器及其多路复用器，直到根任务和未完成工作结束。

## 环境要求

- Linux
- 支持 C++20 的编译器
- CMake 3.20 或更高版本
- `tl-expected`（由 vcpkg manifest 声明）

启用 `io_uring` 还需要 liburing 和 pkg-config。启用 RDMA 需要 `librdmacm`、`libibverbs` 和 pkg-config。默认的 `epoll` 构建不需要这些可选依赖。

## 快速开始

下面的程序运行一个协程，并异步休眠一秒：

```cpp
#include <chrono>
#include <nbio/async.hpp>
#include <nbio/time/timer_service.hpp>

int main() {
    nbio::async::Run([]() -> nbio::async::Task<void> {
        co_await nbio::time::SystemTimeService{}.Sleep(std::chrono::seconds(1));
    }());
}
```

TCP echo server、基于信号的优雅退出以及其他完整示例见 [`examples`](examples/)。

## 配置与构建

默认选择 `epoll` 后端。以下示例使用 vcpkg manifest 提供 `tl-expected`；示例程序默认不构建：

```sh
cmake -S . -B build \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DNBIO_BUILD_EXAMPLES=ON
cmake --build build
```

可选构建设置：

| 设置 | 作用 | 额外依赖 |
| --- | --- | --- |
| `-DNBIO_ENABLE_IO_URING=ON` | 构建 Linux `io_uring` 后端 | liburing 和 pkg-config |
| `-DNBIO_ENABLE_RDMA=ON` | 构建 Linux RDMA 支持及其示例 | `librdmacm`、`libibverbs` 和 pkg-config |
| `-DNBIO_BUILD_EXAMPLES=ON` | 构建示例程序 | CLI11（由 vcpkg `examples` feature 提供） |
| `-DBUILD_TESTING=ON` | 构建测试 | GoogleTest 和 CLI11 |
| `-DBUILD_BENCHMARKS=ON` | 构建基准测试 | Google Benchmark 和 CLI11 |

对应的 vcpkg manifest feature（`io-uring`、`rdma`、`examples`、`testing` 和 `benchmark`）会启用相关 CMake 设置或依赖。显式 CMake 选项优先。构建 RDMA 示例不要求 RDMA 设备，但运行示例需要。

## 在 CMake 项目中使用

安装 NBIO 后，查找 `nbio` CMake package 并链接其导出目标：

```cmake
find_package(nbio CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE nbio::nbio)
```

vcpkg port 位于 [`ports/nbio`](ports/nbio)。NBIO 会传递链接 `tl::expected` 依赖，因此使用 `nbio::nbio` 的消费者也会获得所需的 include 路径。

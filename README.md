# Goodmalloc

这是一个高并发内存池，尤其适合分配和回收大量小对象的场景。

## 构建

在 goodmalloc/ 下运行：

```bash
mkdir build && cd build
cmake -G Ninja ..           # 因为本项目启用了 C++20 Modules，所以必须使用 Ninja
cmake --build . -j$(nproc)  # 使用所有 CPU 核心进行构建
```

至此，您得到了 .so 库文件，它在 build/ 下。

上述构建方法构建的目标是 Debug 版。Debug 版禁用优化，有调试符号。

您可以执行 `./build/benchmark` 来验证构建是否成功，并查看本项目的性能表现。

**可用选项：**

| 选项 | 默认值 | 可选值 |  说明 |
|------|--------|------|------|
| -DCMAKE_BUILD_TYPE | Debug | Debug/Release | 决定构建目标是 Debug 还是 Release |
| -DUSE_ASAN | OFF | ON/OFF | 是否启用 ASAN |
| -DUSE_UBSAN | OFF | ON/OFF | 是否启用 UBSAN |
| -DUSE_TSAN | OFF | ON/OFF | 是否启用 TSAN |
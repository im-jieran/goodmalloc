# Goodmalloc

这是一个高并发内存池，尤其适合分配和回收大量小对象的场景。

## 构建

在 goodmalloc/ 下运行：

```bash
mkdir build && cd build
cmake -G Ninja ..        # 因为本项目启用了 C++20 Modules，所以必须使用 Ninja
cmake --build .
```

至此，您得到了 .so 库文件，它在 build/ 下。
上述构建方法构建的目标是 Debug 版。Debug 版禁用优化，有调试符号。

您可以执行 `./build/benchmark` 来验证构建是否成功，并查看本项目的性能表现。

验证构建可用后，您可以使用 `-DCMAKE_BUILD_TYPE=Release` 构建 Release 版。Release 版有更优越的性能。
#ifndef GOODMALLOC_H
#define GOODMALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // 分配一块大小至少为 size 字节的内存
    void* goodalloc(size_t size);

    // 释放之前由 goodalloc 分配的内存
    void goodfree(void* ptr);

#ifdef __cplusplus
}
#endif

#endif  // GOODMALLOC_H

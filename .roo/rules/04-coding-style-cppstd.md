
### C风格下使用Cpp标准库容器的扩展规范
此扩展规范适用于在C风格代码中引入C++标准库容器（如`std::vector`、`std::map`、`std::string`等），以提升数据处理效率，同时保持整体C风格（即避免C++的类继承、多态等OOP特性，仅将容器作为高级数据结构使用）。文件需以`.cpp`结尾编译为C++，头文件需兼容C/C++（使用`extern "C"`）。优先选择轻量容器，避免性能开销大的特性（如异常处理）。所有C++特性需在条件编译中隔离（如`#ifdef __cplusplus`）。

#### 1. **命名约定扩展**
   - 容器变量命名：遵循原有变量命名规则，小写加下划线；添加容器类型后缀以区分（如`v_sensor_data_vec` for `std::vector<sensor_data_t>`，`m_config_map` for `std::map<uint32_t, std::string>`）。
   - 类型别名：使用`typedef`或`using`定义容器类型别名，以`_t`结尾（如`typedef std::vector<uint8_t> byte_vec_t;`）；在头文件中置于`#ifdef __cplusplus`块内。
   - 避免命名冲突：容器相关函数/宏前缀模块名，并避免与C标准库重名（如不使用`str`作为`std::string`变量）。

#### 2. **代码格式扩展**
   - 模板使用：模板参数保持简洁，换行时对齐（如`std::vector<std::pair<uint32_t, std::string>>`）；长模板声明拆分为多行。
   - 迭代器：使用`auto`简化迭代器声明（如`for (auto it = vec.begin(); it != vec.end(); ++it)`），但需注释说明类型以保持可读性。
   - 命名空间：使用`using namespace std;`仅限于源文件内部函数作用域，避免全局污染；优先显式限定（如`std::vector`）。

#### 3. **数据类型扩展**
   - 优先容器：使用`std::vector`代替动态数组，`std::string`代替`char*`字符串，`std::map`或`std::unordered_map`代替自定义哈希表；避免`std::list`或`std::set`除非必要（性能考虑）。
   - 类型兼容：容器元素类型需与C风格类型一致（如使用固定宽度整数）；避免嵌套容器超过2层以控制复杂度。
   - 转换：显式转换C数组到容器（如`std::vector<uint8_t> vec(arr, arr + size);`），并注释潜在拷贝开销。

#### 4. **函数设计扩展**
   - 参数传递：优先传const引用（如`const std::vector<uint8_t>& vec`）以避免拷贝；返回容器时优先返回值（RVO优化），或使用输出参数指针。
   - 函数封装：为容器操作编写C风格包装函数（如`int vec_append(byte_vec_t* p_vec, uint8_t value);`），隐藏C++细节。
   - 异常处理：禁用异常（编译选项`-fno-exceptions`），使用返回值报告错误（如容器操作失败返回`ERR_FAIL`）。

#### 5. **内存管理扩展**
   - 容器生命周期：容器在栈上声明优先，动态分配仅用于大容器（如`std::unique_ptr<std::vector<uint8_t>>`）；避免`new/delete`，使用智能指针（如`std::unique_ptr`）管理。
   - 释放：容器析构自动释放内存，无需手动`free`；但需确保容器清空（如`vec.clear(); vec.shrink_to_fit();`）以释放资源。
   - 资源约束：监控容器大小（如`vec.reserve(expected_size);`），避免在嵌入式环境中过度增长；优先预分配以减少重分配。

#### 6. **控制流扩展**
   - 迭代：优先范围for循环（如`for (const auto& item : vec) { ... }`），但需注释以说明C风格等价（如传统for循环）。
   - 错误检查：容器操作（如`at()`）需检查边界（优先`[]`但添加手动检查）；避免抛异常的API，使用安全版本。

#### 7. **预处理器扩展**
   - 兼容C/C++：容器声明置于`#ifdef __cplusplus`块内；提供C fallback（如宏定义模拟简单容器）。
   - 包含头文件：使用`<vector>`等在源文件中；头文件中仅声明类型别名，不包含实现。

#### 8. **注释扩展**
   - 容器使用：注释说明为什么选择C++容器（如“使用std::vector以动态管理缓冲区，避免手动realloc”）；描述潜在性能影响。
   - 示例：函数注释包括容器参数的预期大小或约束。

#### 9. **错误处理扩展**
   - 容器错误：将C++异常转换为返回值（如检查`empty()`、`size()`）；日志记录容器相关失败（如“vector resize failed due to memory limit”）。

#### 10. **嵌入式特定扩展**
   - 性能优化：避免STL的线程安全特性；使用`std::allocator`自定义分配器如果需要控制内存。
   - 兼容性：确保代码可编译为纯C（通过条件编译禁用C++部分）；测试在资源受限环境中的行为。

### 示例代码扩展
```cpp
/* serial_comm.h */
#ifndef SERIAL_COMM_H
#define SERIAL_COMM_H

#include <stdint.h>
#include <stdbool.h>

#define ERR_OK      0
#define ERR_FAIL    -1
#define ERR_TIMEOUT -2

#ifdef __cplusplus
#include <vector>
typedef std::vector<uint8_t> byte_vec_t;
#else
/* C fallback: use struct with manual management */
typedef struct {
    uint8_t* p_data;
    size_t size;
    size_t capacity;
} byte_vec_t;
#endif

/* Appends data to vector. Returns ERR_OK on success. */
int vec_append(byte_vec_t* p_vec, uint8_t value);

#endif /* SERIAL_COMM_H */

/* serial_comm.cpp */
#include "serial_comm.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#ifdef __cplusplus
int vec_append(byte_vec_t* p_vec, uint8_t value) {
    if (!p_vec) {
        return ERR_FAIL;
    }
    try {
        p_vec->push_back(value);  /* No exceptions enabled, but check size */
        if (p_vec->size() > p_vec->capacity()) {
            return ERR_FAIL;  /* Hypothetical overflow check */
        }
    } catch (...) {
        return ERR_FAIL;  /* Fallback, though exceptions disabled */
    }
    return ERR_OK;
}
#else
/* C implementation */
int vec_append(byte_vec_t* p_vec, uint8_t value) {
    /* Manual realloc logic... */
    return ERR_OK;
}
#endif
```

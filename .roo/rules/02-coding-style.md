### C语言代码规范

#### 1. **命名约定**
   - **文件命名**：小写字母和下划线，头文件以“.h”结尾，源文件以“.c”结尾，文件名反映模块功能（如`serial_comm.c`）。
   - **变量命名**：小写加下划线，描述性强；
     - 对外可见全局变量前缀`g_`（应极力避免该情况）；
     - 内部私有的静态全局变量前缀`s_`，
     - 指针前缀`p_`，
     - 布尔变量前缀`b_`并用疑问形式（如`b_is_connected`）。
     - 局部别名变量优先使用`const`（如`const int b = a`）。
     - 指针类型根据情况优先使用`const`（如`const int *p_b = &a`, `int * const p_b = &a`）。
   - **函数命名**：小写加下划线，模块相关函数加模块前缀（如`serial_open()`）；避免C标准库函数名。
   - **类型命名**：优先使用`typedef`定义结构/枚举，
     - 结构体类型名以`_t`结尾（如`struct sensor_data {} sensor_data_t`）；
     - 枚举类型名以`_et`结尾（如`enum xxx {} xxx_et`）；
     - 优先使用固定宽度类型（如`uint8_t`、`int32_t`）。
   - **宏命名**：全大写加下划线（如`MAX_BUFFER_SIZE`）；避免与标准库宏冲突。

#### 2. **代码格式**
   - **缩进**：使用4空格缩进，禁止制表符。
   - **行宽**：每行不超过80字符，合理拆分长语句。
   - **大括号**：函数/控制结构的大括号置于行尾，单独语句也需大括号包裹。例如：
     ```c
     if (condition) {
         do_something();
     }
     ```
   - **空格**：关键词（如`if`、`while`）后加空格；二元运算符（如`+`、`=`）前后加空格；函数调用括号前无空格（如`func(arg)`）。
   - **空行**：逻辑块间加单空行；函数间加两空行；文件末尾加空行。

#### 3. **数据类型**
   - 优先使用固定宽度类型（如`uint16_t`）以确保跨平台一致性；尽量避免`int`、`char`等模糊类型。
   - `char`仅用于字符串；布尔类型使用`stdbool.h`的`bool`、`true`、`false`。
   - 显式类型转换需注释说明理由，避免隐式转换导致溢出。
   - 结构和枚举使用`typedef`，避免重复`struct`关键字。

#### 4. **函数设计**
   - 函数短小，尽量不超过50行，功能单一。
   - 参数和局部变量总数不超过7个；优先传指针而非大结构。
   - 优先`static`函数限制模块内可见。
   - 返回值明确，优先用`int`或枚举或布尔值表示状态。

#### 5. **内存管理**
   - 谨慎使用动态内存（`malloc`/`free`），优先静态分配以降低碎片风险。
   - 分配内存后立即检查返回指针是否为`NULL`。
   - 释放内存后将指针置为`NULL`。
   - 使用`const`修饰只读指针参数，防止意外修改。

#### 6. **控制流**
   - `if-else`链以`else`结束，`switch`必须包含`default`。
   - 避免嵌套超过3层；复杂逻辑拆分为函数。
   - 禁止`goto`，除非用于统一错误处理（如资源清理）。
   - 循环体用大括号，即使单行。

#### 7. **预处理器**
   - 头文件使用`#ifndef`防止重复包含，格式如：
     ```c
     #ifndef MODULE_NAME_H
     #define MODULE_NAME_H
     #ifdef __cplusplus
     extern "C" {
     #endif
    
     // ...

     #ifdef __cplusplus
     }
     #endif
     /* 内容 */
     #endif /* MODULE_NAME_H */
     ```
   - 宏定义参数加括号，多语句宏用`do { } while (0)`包裹。
   - 最小化条件编译（`#ifdef`），优先用运行时检查。

#### 8. **注释**
   - 注释说明“为什么”而非“做什么”；使用`/* */`风格。
   - 每个函数前加注释，说明功能、参数、返回值和可能的错误。
   - 尽量避免注释掉代码，删除无用代码。优先使用`#if 0 #endif`
   - 关键变量声明需注释说明用途。

#### 9. **错误处理**
   - 函数返回错误码或状态，使用明确枚举（如`enum error_t { ERR_OK, ERR_FAIL }`）。
   - 对外接口检查重要的外部输入（如文件、用户输入）并验证范围。
   - 内部接口执行相较于更宽松的验证


### 示例代码
```c
/* serial_comm.h */
#ifndef SERIAL_COMM_H
#define SERIAL_COMM_H

#include <stdint.h>
#include <stdbool.h>

#define ERR_OK      0
#define ERR_FAIL    -1
#define ERR_TIMEOUT -2

/* Opens serial port with given baud rate. Returns ERR_OK on success. */
int serial_open(const char *p_device, uint32_t baud_rate);

#endif /* SERIAL_COMM_H */

/* serial_comm.c */
#include "serial_comm.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

static int g_serial_fd = -1; /* File descriptor for serial port */

int serial_open(const char *p_device, uint32_t baud_rate)
{
    if (!p_device) {
        return ERR_FAIL;
    }

    g_serial_fd = open(p_device, O_RDWR | O_NOCTTY);
    if (g_serial_fd < 0) {
        fprintf(stderr, "Failed to open %s: %d\n", p_device, errno);
        return ERR_FAIL;
    }

    /* Configure baud rate (simplified) */
    if (baud_rate == 0) {
        return ERR_FAIL;
    }

    return ERR_OK;
}
```

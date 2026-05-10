---
description: 代码命名规范 - 变量、函数、类、文件等命名约定
globs: "**/*.{c,h,js,ts,jsx,tsx,py,java,go,rs,rb,php,cpp,hpp,cs,css,scss,html,vue,svelte}"
---

# 命名规范 Naming Conventions

在生成或重构代码时，你**必须**严格遵守以下命名约定。任何与此规则冲突的命名都被视为错误。

## 通用规则 (All Languages)

### 文件与目录
- **C/C++ 源文件与头文件** MUST 使用 `snake_case`，如 `user_profile.c`、`user_profile.h`。
- **目录命名** MUST 使用 `kebab-case`，如 `user-profile/`。
- **测试文件** MUST 与被测文件同名，后缀为 `.test.{ext}` 或 `.spec.{ext}`。
- **避免**使用空格或大写字母命名文件（除组件文件外）。

### 变量与函数
- **C 语言** 变量与函数 MUST 使用 `snake_case`：`user_name`、`calculate_total()`（见语言特定章节）。
- **常量** MUST 使用 `UPPER_SNAKE_CASE`：`MAX_RETRY_COUNT`、`API_BASE_URL`。
- **枚举成员** MUST 使用 `UPPER_SNAKE_CASE`。
- **私有字段/方法**：按语言习惯处理，避免使用无意义的 `_` 前缀（Python 除外）。
- **禁止单字母变量**（循环索引 `i`、`j` 除外），名称必须自解释。

### 布尔变量
- MUST 使用 `is`、`has`、`can`、`should` 前缀：`is_active`、`has_permission`。
- 禁用双重否定，如 `is_not_disabled` → `is_enabled`。

### 集合/数组
- MUST 使用复数名词或添加 `List`/`Map` 后缀：`users`、`user_list`、`name_map`。

## 语言/框架特定

### C
- **源文件/头文件**：`snake_case`，如 `user_profile.c`、`order_service.h`。
- **变量、函数**：`snake_case`，如 `user_count`、`parse_config()`。
- **宏、常量**：`UPPER_SNAKE_CASE`，如 `BUFFER_SIZE`。
- **结构体/联合体/枚举类型**：`snake_case`，并在全项目保持一致。
- **枚举成员**：`UPPER_SNAKE_CASE`。
- **禁止缩写**：严禁使用 `usrCnt` 等模糊缩写，应写为 `user_count`；公认缩写 `id`、`url`、`api` 等除外。

## 代码格式 (Code Formatting)

- **禁止单行堆砌多条逻辑语句**：控制流语句（`if`、`for`、`while`）的 body 如果包含多条**数据操作/业务逻辑**语句，MUST 换行而非挤在一行。
  - ❌ 错误（数据操作堆砌）：
    ```c
    if (!w->buf) { ctx->errors++; free(w); ctx->op_idx++; continue; }
    ```
  - ✅ 正确：
    ```c
    if (!w->buf) {
        ctx->errors++;
        free(w);
        ctx->op_idx++;
        continue;
    }
    ```

- **函数调用参数不换行**：如果函数调用的参数列表可以在一行内写完，MUST 保持在同一行，不应无意义地换行。
  - ❌ 错误：
    ```c
    some_function(arg1, arg2,
                  arg3);
    ```
  - ✅ 正确（参数少、一行能容纳）：
    ```c
    some_function(arg1, arg2, arg3);
    ```

## 禁止事项
- **缩写滥用**：严禁 `usrCnt`、`msgTxt` 等模糊缩写，应写为 `user_count` 或 `userCount`。仅允许公认缩写：`id`、`url`、`api`、`db` 等广泛接受的形式。
- **魔数嵌入**：禁止 `threshold5` 之类写法，应当使用常量。
- **拼音/中英混合**：所有标识符必须使用英文单词。

## 执行要求
- 生成代码前先自检命名是否符合以上规范。
- 重构命名时必须严格遵循，如有偏离需注释说明理由。


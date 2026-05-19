cmake_minimum_required(VERSION 3.14)

# ── 编译器强制要求 clang++（必须在 project() 之前）─────────────────
find_program(CLANGXX clang++ REQUIRED)
set(CMAKE_CXX_COMPILER "${CLANGXX}" CACHE STRING "" FORCE)
message(STATUS "使用编译器: ${CLANGXX}")

project(TinyWebServer LANGUAGES CXX)

# ── 编译标准 ──────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 默认 Debug 构建；Release 构建：cmake -DCMAKE_BUILD_TYPE=Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# 生成 compile_commands.json（供 clangd / clang-tidy 使用）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── 依赖查找 ─────────────────────────────────────────────────────
find_package(Threads REQUIRED)

# MySQL client 库
find_library(MYSQL_LIB NAMES mysqlclient
    HINTS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib)
if(NOT MYSQL_LIB)
    message(FATAL_ERROR "找不到 mysqlclient 库，请安装 libmysqlclient-dev 或 mariadb-libs")
endif()

find_path(MYSQL_INCLUDE_DIR NAMES mysql/mysql.h
    HINTS /usr/include /usr/local/include)
if(NOT MYSQL_INCLUDE_DIR)
    message(FATAL_ERROR "找不到 mysql/mysql.h 头文件")
endif()

# 将 MySQL 封装为 INTERFACE target，方便子目录复用
add_library(mysql_iface INTERFACE)
target_include_directories(mysql_iface INTERFACE ${MYSQL_INCLUDE_DIR})
target_link_libraries(mysql_iface INTERFACE ${MYSQL_LIB})

# ── 业务源文件（服务器与测试共用） ────────────────────────────────
# 将可复用的源文件封装为 OBJECT library，避免重复编译
add_library(server_objs OBJECT
    log/log.cpp
    CGImysql/sql_connection_pool.cpp
    http/http_conn.cpp
    timer/lst_timer.cpp
    webserver.cpp
    config.cpp
)
target_include_directories(server_objs PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(server_objs PUBLIC Threads::Threads mysql_iface)

# ── 编译期静态校验（Issue #11）────────────────────────────────────
# 参考 arkui_ace_engine BUILD.gn、LLVM、Chromium、FFmpeg 编译配置
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")

    target_compile_options(server_objs PRIVATE
        # ── 基础质量（LLVM / Chromium 基线）──────────────────────────
        -Wall
        -Wextra
        -Wshadow
        -Wnull-dereference
        -Wformat=2
        -Wcast-qual
        -Wold-style-cast

        # ── 返回值与控制流 ── Issue #1 ────────────────────────────────
        -Werror=return-type

        # ── 整数与类型安全（arkui BUILD.gn L59-60）────────────────────
        -Werror=int-to-pointer-cast
        -Werror=pointer-to-int-cast
        -Werror=int-in-bool-context
        -Werror=incompatible-pointer-types
        -Wnarrowing                        # Issue #6: int 隐式转为 string
        -Wconversion
        -Wsign-compare
        -Wstring-conversion                # Issue #6

        # ── 内存与未定义行为（arkui BUILD.gn L49 L55）─────────────────
        -Werror=return-stack-address
        -Werror=null-pointer-arithmetic    # Issue #4
        -Warray-bounds                     # Issue #5
        -Wuninitialized                    # Issue #2

        # ── 枚举安全（arkui BUILD.gn L50 L56）────────────────────────
        -Werror=anon-enum-enum-conversion
        -Werror=enum-compare-switch

        # ── 现代 C++ 质量（arkui BUILD.gn L65）───────────────────────
        -Werror=range-loop-construct
        -Werror=abstract-final-class

        # ── 可移植性（arkui BUILD.gn L53）────────────────────────────
        -Werror=nonportable-include-path

        # ── 代码意图（arkui BUILD.gn L57 L58 L62）────────────────────
        -Werror=xor-used-as-pow
        -Werror=absolute-value
        -Werror=sizeof-array-div
    )

    # ── Debug 构建：运行期检测（FFmpeg / Chromium 策略）──────────────
    # ASan 捕获堆/栈溢出、use-after-free；UBSan 捕获整数溢出、NULL 解引用

    # ── CI 模式：零警告策略（Chromium 策略）──────────────────────────
    # 本地开发保持宽松；CI 传入 -DWARNINGS_AS_ERRORS=ON 强制零警告
    option(WARNINGS_AS_ERRORS "将所有警告视为错误" OFF)
    if(WARNINGS_AS_ERRORS)
        target_compile_options(server_objs PRIVATE -Werror)
    endif()

endif()

# ── 可执行文件：server ────────────────────────────────────────────
add_executable(server main.cpp $<TARGET_OBJECTS:server_objs>)

# Debug 构建启用 ASan/UBSan（仅作用于 server 可执行文件，不影响测试目标）
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(server PRIVATE
        -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer)
    target_link_options(server PRIVATE -fsanitize=address -fsanitize=undefined)
endif()
target_link_libraries(server PRIVATE Threads::Threads mysql_iface)

# ── 测试 ─────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(test)

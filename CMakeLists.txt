cmake_minimum_required(VERSION 3.14)
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

# ── 可执行文件：server ────────────────────────────────────────────
add_executable(server main.cpp $<TARGET_OBJECTS:server_objs>)
target_link_libraries(server PRIVATE Threads::Threads mysql_iface)

# ── 测试 ─────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(test)

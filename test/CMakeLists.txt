find_package(GTest REQUIRED)

# block_queue 是纯头文件模板，不依赖任何 .cpp，单独编译最快
add_executable(test_block_queue test_block_queue.cpp)
target_include_directories(test_block_queue PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(test_block_queue PRIVATE GTest::gtest_main Threads::Threads)

# HTTP 解析器与定时器测试复用 server_objs（已包含 mysql_iface、Threads）
add_executable(test_http_parser test_http_parser.cpp $<TARGET_OBJECTS:server_objs>)
target_include_directories(test_http_parser PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(test_http_parser PRIVATE GTest::gtest_main Threads::Threads mysql_iface)

add_executable(test_timer test_timer.cpp $<TARGET_OBJECTS:server_objs>)
target_include_directories(test_timer PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(test_timer PRIVATE GTest::gtest_main Threads::Threads mysql_iface)

# 注册到 ctest
include(GoogleTest)
gtest_discover_tests(test_block_queue)
gtest_discover_tests(test_http_parser)
gtest_discover_tests(test_timer)

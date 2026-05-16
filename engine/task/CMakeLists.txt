# CP9: Task System (任务系统)
# 事件驱动 + 声明式条件 + 可回放

cmake_minimum_required(VERSION 3.20)

project(next_task)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件
set(TASK_SOURCES
    src/task_system.cpp
)

# 头文件
set(TASK_HEADERS
    include/next/task/task_system.h
)

# 创建静态库
add_library(next_task STATIC
    ${TASK_SOURCES}
    ${TASK_HEADERS}
)

# 设置包含目录
target_include_directories(next_task PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/engine/runtime/include
    ${CMAKE_SOURCE_DIR}/engine/foundation/include
    ${CMAKE_SOURCE_DIR}/engine/platform/include
    ${CMAKE_SOURCE_DIR}/engine/serialization/include
)

# 链接依赖
target_link_libraries(next_task PUBLIC
    next_runtime
    next_foundation
    next_serialization
)

# Windows特定设置
if(WIN32)
    target_compile_definitions(next_task PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# 编译选项
if(MSVC)
    target_compile_options(next_task PRIVATE
        /W4
        /wd4996  # 禁用某些安全警告
    )
else()
    target_compile_options(next_task PRIVATE
        -Wall
        -Wextra
        -Wformat=2
        -Werror=format
        -Wno-unused-parameter
    )
endif()

# Release优化 - 使用生成器表达式以支持多配置生成器
if(MSVC)
    target_compile_options(next_task PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
    )
    target_link_options(next_task PRIVATE "$<$<CONFIG:Release>:/LTCG>")
else()
    target_compile_options(next_task PRIVATE "$<$<CONFIG:Release>:-O3 -march=native>")
endif()

# 安装
install(TARGETS next_task
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${TASK_HEADERS}
    DESTINATION include/next/task
)

# 序列化系统
cmake_minimum_required(VERSION 3.20)

project(next_serialization)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件
set(SERIALIZATION_SOURCES
    src/serialization.cpp
)

# 头文件
set(SERIALIZATION_HEADERS
    include/next/serialization/serialization.h
)

# 创建静态库
add_library(next_serialization STATIC
    ${SERIALIZATION_SOURCES}
    ${SERIALIZATION_HEADERS}
)

# 设置包含目录
target_include_directories(next_serialization PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/engine/foundation/include
)

# 链接依赖
target_link_libraries(next_serialization PUBLIC
    next_foundation
)

# Windows特定设置
if(WIN32)
    target_compile_definitions(next_serialization PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# 编译选项
if(MSVC)
    target_compile_options(next_serialization PRIVATE
        /W4
        /wd4996
    )
else()
    target_compile_options(next_serialization PRIVATE
        -Wall
        -Wextra
        -Wformat=2
        -Werror=format
        -Wno-unused-parameter
    )
endif()

# Release优化 - 使用生成器表达式以支持多配置生成器
if(MSVC)
    target_compile_options(next_serialization PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
    )
    target_link_options(next_serialization PRIVATE "$<$<CONFIG:Release>:/LTCG>")
else()
    target_compile_options(next_serialization PRIVATE "$<$<CONFIG:Release>:-O3 -march=native>")
endif()

# 安装
install(TARGETS next_serialization
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${SERIALIZATION_HEADERS}
    DESTINATION include/next/serialization
)

# CP8: Script System (脚本系统)
# 框架实现版本 - 可在没有Lua库的情况下编译

cmake_minimum_required(VERSION 3.20)

project(next_script)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件
set(SCRIPT_SOURCES
    src/script_system.cpp
)

# 头文件
set(SCRIPT_HEADERS
    include/next/script/script_system.h
)

# 创建静态库
add_library(next_script STATIC
    ${SCRIPT_SOURCES}
    ${SCRIPT_HEADERS}
)

# 设置包含目录
target_include_directories(next_script PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/engine/runtime/include
    ${CMAKE_SOURCE_DIR}/engine/foundation/include
    ${CMAKE_SOURCE_DIR}/engine/platform/include
)

# 链接依赖
target_link_libraries(next_script PUBLIC
    next_runtime
    next_foundation
)

# Lua库集成（可选）
option(USE_SYSTEM_LUA "Use system Lua library" OFF)

if(USE_SYSTEM_LUA)
    find_package(Lua QUIET)
    if(LUA_FOUND)
        message(STATUS "CP8: System Lua support enabled (${LUA_VERSION_STRING})")
        target_include_directories(next_script PRIVATE ${LUA_INCLUDE_DIR})
        target_link_libraries(next_script PUBLIC ${LUA_LIBRARIES})
        target_compile_definitions(next_script PUBLIC NEXT_WITH_LUA=1)
    else()
        message(WARNING "CP8: USE_SYSTEM_LUA=ON but no system Lua was found. Falling back to stub mode.")
    endif()
else()
    message(STATUS "CP8: Using framework implementation (no Lua dependency)")
    message(STATUS "CP8: To enable full Lua support, set USE_SYSTEM_LUA=ON")
endif()

# Windows特定设置
if(WIN32)
    target_compile_definitions(next_script PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

# 编译选项
if(MSVC)
    target_compile_options(next_script PRIVATE
        /W4
        /wd4996  # 禁用某些安全警告
    )
else()
    target_compile_options(next_script PRIVATE
        -Wall
        -Wextra
        -Wformat=2
        -Werror=format
        -Wno-unused-parameter
    )
endif()

# Release优化 - 使用生成器表达式以支持多配置生成器
if(MSVC)
    target_compile_options(next_script PRIVATE
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
    )
    target_link_options(next_script PRIVATE "$<$<CONFIG:Release>:/LTCG>")
else()
    target_compile_options(next_script PRIVATE "$<$<CONFIG:Release>:-O3 -march=native>")
endif()

# 安装
install(TARGETS next_script
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${SCRIPT_HEADERS}
    DESTINATION include/next/script
)

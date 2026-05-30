cmake_minimum_required(VERSION 3.20)
project(DriverMouseMacro CXX)

set(CMAKE_CXX_STANDARD 17)

# 包含头文件路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(DriverMouseMacro main.cpp)

# 核心修改：直接传入带有后缀的完整文件路径，不给 MinGW 瞎找的机会
target_link_libraries(DriverMouseMacro "${CMAKE_CURRENT_SOURCE_DIR}/lib/interception.dll")

add_custom_command(TARGET DriverMouseMacro POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/interception.dll"
        $<TARGET_FILE_DIR:DriverMouseMacro>
)
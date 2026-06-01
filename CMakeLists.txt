cmake_minimum_required(VERSION 3.20)
project(DriverMouseMacro CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==========================================
# 1. 定义本地的路径
# ==========================================
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui")
set(INTERCEPTION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/interception")

# ==========================================
# 2. 使用本地路径收集源码
# ==========================================
set(IMGUI_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
        ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

# 3. 包含头文件路径
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${INTERCEPTION_DIR}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

# 4. 应用源码(按职责拆分)
set(APP_SOURCES
        src/core/app_state.cpp
        src/core/clicker.cpp
        src/ui/gui.cpp
        src/platform/config.cpp
        src/platform/d3d_window.cpp
)

# WIN32 -> 不显示控制台窗口
add_executable(DriverMouseMacro WIN32 main.cpp ${APP_SOURCES} ${IMGUI_SOURCES})

# 链接 Interception 用户态 shim + DX11 渲染所需库
target_link_libraries(DriverMouseMacro
        "${INTERCEPTION_DIR}/interception.dll"
        d3d11
        dxgi
        d3dcompiler
        dwmapi
)

add_custom_command(TARGET DriverMouseMacro POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${INTERCEPTION_DIR}/interception.dll"
        $<TARGET_FILE_DIR:DriverMouseMacro>
)

target_link_options(DriverMouseMacro PRIVATE
        -static-libgcc -static-libstdc++ -static -lpthread)

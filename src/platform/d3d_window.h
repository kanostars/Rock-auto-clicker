#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>

namespace app {

// DX11 + Win32 设备/交换链管理
bool   create_device_d3d(HWND hWnd);
void   cleanup_device_d3d();
void   create_render_target();
void   cleanup_render_target();

ID3D11Device*           d3d_device();
ID3D11DeviceContext*    d3d_context();
IDXGISwapChain*         d3d_swap_chain();
ID3D11RenderTargetView* d3d_main_rtv();

// 待处理的 resize 尺寸(WM_SIZE 写入,主循环读取后清零)
extern UINT g_resize_width;
extern UINT g_resize_height;

LRESULT WINAPI wnd_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 从文件加载 PNG/JPG/BMP 并创建 D3D11 ShaderResourceView
// 返回 nullptr 表示加载失败
ID3D11ShaderResourceView* load_texture(const char* path, int* out_w = nullptr, int* out_h = nullptr);

} // namespace app

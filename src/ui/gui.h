#pragma once

#include <d3d11.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace app {

void render_gui();
void set_logo_texture(ID3D11ShaderResourceView* srv, int w, int h);
void set_gui_hwnd(HWND hwnd);

} // namespace app

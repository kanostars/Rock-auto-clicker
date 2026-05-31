#pragma once

namespace app {

// 后台连点线程:按 param_* 参数注入鼠标点击
[[noreturn]] void macro_worker();

// Interception 事件循环:记录活跃鼠标、监听侧键 (Button 4) 切换连点
[[noreturn]] void interception_thread();

} // namespace app

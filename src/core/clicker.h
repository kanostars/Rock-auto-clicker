#pragma once

namespace app {

[[noreturn]] void macro_worker();
[[noreturn]] void interception_thread();
// 纯键盘/无鼠标侧键组合的轮询触发线程
[[noreturn]] void hotkey_poller();

} // namespace app

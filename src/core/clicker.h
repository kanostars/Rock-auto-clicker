#pragma once

namespace app {

[[noreturn]] void macro_worker();
[[noreturn]] void interception_thread();
[[noreturn]] void hotkey_poller();

} // namespace app

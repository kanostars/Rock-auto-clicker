#pragma once

namespace app {

void load_config(); // 启动时读取,不存在则保持默认值
void save_config(); // 退出时写入

} // namespace app

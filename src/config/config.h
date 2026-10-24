#pragma once
// Config 装配门面（← config/config.rs：Config{clash_config, verge_config,
// profiles_config} 的 Draft 三件套；runtime_config 属 T08）
// T03 职责边界：只做持久化层装配 + 首启生成 + 迁移编排；
// enhance 流水线/校验/内核管理一概不在此处（T07/T08）。
#include <memory>

#include "clashwin/config/models.h"
#include "draft.h"

namespace clashwin::config {

class Config {
 public:
  // 单例（← Config::global）
  static Config& instance();

  Draft<IClashTemp>& clash() { return clash_; }
  Draft<IVerge>& verge() { return verge_; }
  Draft<IProfiles>& profiles() { return profiles_; }

  // ← Config::init_config 的持久化部分：
  // 1. 确保 profiles.yaml + 全局 Merge/Script 保护件
  // 2. 非 admin 且服务未装 → enable_tun_mode 强制关（BUG-298 教训的配置面）
  // 3. 孤立 profile 文件清理
  // （generate_and_validate 属 T07/T08，此处不实现）
  // 首启（无任何 yaml）会生成 verge.yaml / config.yaml / profiles.yaml。
  // force_save=true 时无论是否有变更都落三份文件（供测试 / 首启兜底）。
  Result<void> init(bool force_save = false);

  // ← Config::apply_all_and_save_file：三份草稿提交 + 落盘（退出序列调用）
  void apply_all_and_save_file();

 private:
  Config();
  bool is_admin() const;
  bool is_service_installed() const;

  Draft<IClashTemp> clash_;
  Draft<IVerge> verge_;
  Draft<IProfiles> profiles_;
};

}  // namespace clashwin::config

#pragma once
// F-D01：UI 文案统一取 Mini locales（13 语言 × 10 命名空间）。
// ← MF:pages/_layout.tsx:203 const { t } = useTranslation()
// ← MF:services/i18n.ts:147-171 init / changeLanguage / resolveLanguage
// 约定：key 必须能在 ClashVerge/src/locales/zh/<ns>.json 找到；
//       Mini 自身硬编码英文或中文的例外（theme-settings-card 双滑块标签、
//       图表 tooltip 兜底、语言下拉的自名标签）不走 tr()，见
//       evidence/2026-11-06-agreements-conflict.md §4 C-3/C-4/C-5。
#include <map>
#include <string>

#include "i18n/i18n.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/utf.h"

namespace clashwin::ui {

// 生产由 App::init 显式 init()（可在 UI 创建前注入 verge.language）。
// 测试/独立进程首次取译文时，从 exe 同级 locales/ 懒加载兜底
// （资源由 cmake/copy_resources.cmake 的 POST_BUILD 部署）。
inline void ensure_i18n() {
  auto& i18n = clashwin::i18n::I18n::instance();
  if (i18n.inited()) return;
  if (!i18n.init(clashwin::util::dirs::exe_dir() + L"\\locales",
                 clashwin::i18n::I18n::kFallback)) {
    CWLOG_WARN("I18n: locales 未完整加载，缺失 key 将按 i18next 语义显示 key 原文");
  }
}

inline std::wstring tr(const char* key) {
  ensure_i18n();
  return clashwin::utf::utf8_to_utf16(
      clashwin::i18n::I18n::instance().t(key));
}

// i18next 插值 {{name}}（如 "当前版本: {{version}}"）
inline std::wstring tr(const char* key,
                       const std::map<std::string, std::string>& args) {
  ensure_i18n();
  return clashwin::utf::utf8_to_utf16(
      clashwin::i18n::I18n::instance().t(key, args));
}

// UTF-8 版本：通知条（NoticeService 内部存 UTF-8）等非绘制场景用，
// 省掉 utf16 往返。
inline std::string tr8(const char* key,
                       const std::map<std::string, std::string>& args = {}) {
  ensure_i18n();
  return clashwin::i18n::I18n::instance().t(key, args);
}

}  // namespace clashwin::ui

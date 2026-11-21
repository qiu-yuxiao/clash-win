#pragma once
// 弹窗族公共三段几何与版本串辅助（拆分自 utility_modals.cpp）
// src: src/utils/popup-styles.ts:191-246
#include <string>

#include "ui/theme/theme.h"

namespace clashwin::ui::scene::detail {

// src: src/utils/popup-styles.ts:198-241
inline constexpr float kPopupPadX = 14.0f;         // src: src/utils/popup-styles.ts:198 px: 1.75
inline constexpr float kPopupTitlePadY = 10.8f;    // src: src/utils/popup-styles.ts:199 py: 1.35
inline constexpr float kPopupContentPadY = 12.0f;  // src: src/utils/popup-styles.ts:220 py: 1.5
inline constexpr float kPopupActionsPadY = 10.8f;  // src: src/utils/popup-styles.ts:240 py: 1.35
inline constexpr float kPopupActionsGap = 8.0f;    // src: src/utils/popup-styles.ts:241 gap: 1
inline constexpr float kPopupBtnLineH = 1.75f;     // src: node_modules/@mui/material/Button/Button.js lineHeight 1.75
inline constexpr float kPopupLabelLineH = 1.4f;    // src: src/pages/_layout/hooks/use-custom-theme.ts:220 body2 lineHeight 1.4
// src: node_modules/@mui/material/InputBase/InputBase.js:76-78 MUI TextField size="small" 高 40
inline constexpr float kPopupFieldH = 40.0f;
// src: src/pages/_layout/components/dialogs/edit-profile-dialog.tsx fontSize 15.5 / lineHeight 1.35
inline constexpr float kPopupTitleTextH = 15.5f * 1.35f;
inline constexpr float kPopupTitleBlockH =
    kPopupTitlePadY * 2.0f + kPopupTitleTextH + 1.0f;
// src: src/pages/_layout/hooks/use-custom-theme.ts:219-221 body2 = 12.5 * 1.4
inline constexpr float kPopupBody2H = 12.5f * kPopupLabelLineH;
// src: src/pages/_layout/components/dialogs/client-update-dialog.tsx p 1.25 / fontSize 12 / lineHeight 1.5 / maxHeight 160
inline constexpr float kChgPad = 10.0f;
inline constexpr float kChgLineH = 12.0f * 1.5f;
inline constexpr float kChgMaxH = 160.0f;

// src: src/utils/popup-styles.ts:231-246
inline float popup_btn_h(const char* ctype, float font_size, float pad_y) {
  return font_size * kPopupBtnLineH + pad_y * 2.0f +
         theme::ThemeService::instance().button_paint(ctype).border_w * 2.0f;
}

// src: src/pages/_layout/utils/style-helpers.tsx:221-225 formatCoreVersion
inline std::string format_core_version(const std::string& version) {
  if (version.empty()) return "";
  size_t i = 0;
  while (i < version.size() && (version[i] == 'v' || version[i] == 'V')) ++i;
  return "Ver." + version.substr(i);
}

// src: src/pages/_layout/components/dialogs/client-update-dialog.tsx
inline std::string ensure_v_prefix(const std::string& version) {
  if (version.empty()) return "";
  if (version[0] == 'v' || version[0] == 'V') return version;
  return "v" + version;
}

inline std::string strip_ver_prefix(std::string ver) {
  auto trim_ws = [](std::string& s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
  };
  trim_ws(ver);
  if (ver.rfind("Ver.", 0) == 0 || ver.rfind("ver.", 0) == 0) {
    ver.erase(0, 4);  // src: src/pages/_layout/hooks/use-core-update.ts 剥离 "Ver."
    trim_ws(ver);
  }
  while (!ver.empty() && (ver[0] == 'v' || ver[0] == 'V')) {
    ver.erase(0, 1);
  }
  trim_ws(ver);
  return ver;
}

inline bool versions_equal(const std::string& a, const std::string& b) {
  const std::string sa = strip_ver_prefix(a);
  const std::string sb = strip_ver_prefix(b);
  return !sa.empty() && !sb.empty() && sa == sb;
}

}  // namespace clashwin::ui::scene::detail

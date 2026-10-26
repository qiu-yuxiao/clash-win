// IconW 实现（T10）：@mui/icons-material → Segoe Fluent/MDL2 字形映射。
// 原版实际引用清单（01 前端普查 grep 全量）：
//   Close/Clear、Minimize、CropSquare、FilterNone、Refresh、Delete、Info、
//   HelpOutline、Inbox、KeyboardReturn、ArrowUp/Downward、Code、ViewModule、
//   Home、LockOpen、KeyboardArrowUp、PushPin
#include "icon_w.h"

#include <cstring>

namespace clashwin::ui {

IconW::IconW(Widget* parent) : Widget(parent) {}

wchar_t IconW::glyph_for(const std::string& mui_name) {
  static const std::unordered_map<std::string, wchar_t> kMap = {
      {"close", 0xE711},        // Cancel（X）
      {"clear", 0xE711},
      {"minimize", 0xE921},     // ChromeMinimize
      {"maximize", 0xE922},     // ChromeMaximize（CropSquare）
      {"cropsquare", 0xE922},
      {"restore", 0xE923},      // ChromeRestore（FilterNone）
      {"filternone", 0xE923},
      {"refresh", 0xE72C},
      {"delete", 0xE74D},
      {"info", 0xE946},
      {"help", 0xE897},         // Help（HelpOutline 近似）
      {"helpoutline", 0xE897},
      {"inbox", 0xE715},        // Mail 近似（原版空态图标）
      {"keyboardreturn", 0xE751},
      {"arrowupward", 0xE74A},
      {"arrowdownward", 0xE74B},
      {"keyboardarrowup", 0xE70E},    // ChevronUp
      {"keyboardarrowdown", 0xE70D},  // ChevronDown
      {"code", 0xE943},
      {"viewmodule", 0xE71D},   // AllApps 近似
      {"home", 0xE80F},
      {"lockopen", 0xE785},     // LockOpened
      {"pushpin", 0xE718},      // Pinned
      {"settings", 0xE713},
      {"search", 0xE721},
      {"chevronright", 0xE76C},
      {"chevronleft", 0xE76B},
      {"check", 0xE73E},
      {"sync", 0xE777},
      {"play", 0xE768},
      {"pause", 0xE769},
  };
  std::string key;
  key.reserve(mui_name.size());
  for (char c : mui_name) {
    if (c == '_' || c == ' ' || c == '-') continue;
    key += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  }
  // 去 MUI 风格后缀（CloseRounded/RefreshOutlined → close/refresh）
  for (const char* suffix : {"rounded", "outlined", "sharp", "twotone"}) {
    size_t n = std::strlen(suffix);
    if (key.size() > n && key.compare(key.size() - n, n, suffix) == 0) {
      key.resize(key.size() - n);
      break;
    }
  }
  auto it = kMap.find(key);
  return it != kMap.end() ? it->second : 0;
}

void IconW::set_icon(const std::string& mui_name) {
  glyph_ = glyph_for(mui_name);
  invalidate();
}

void IconW::set_glyph(wchar_t g) {
  glyph_ = g;
  invalidate();
}

SizeF IconW::on_measure(float max_w, float max_h) {
  (void)max_w; (void)max_h;
  return {size_, size_};
}

void IconW::on_paint(IRenderTarget& rt) {
  if (!glyph_) return;
  TextStyle ts;
  ts.font_family = font_fallback_ ? L"Segoe MDL2 Assets" : L"Segoe Fluent Icons";
  ts.size = size_;
  ts.color = color_;
  ts.align_h = AlignH::Center;
  ts.align_v = AlignV::Center;
  rt.draw_text(std::wstring(1, glyph_), {0, 0, bounds().w, bounds().h}, ts);
}

}  // namespace clashwin::ui

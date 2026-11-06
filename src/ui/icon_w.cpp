// IconW 实现（T10）：@mui/icons-material → Segoe Fluent/MDL2 字形映射。
// 原版实际引用清单（01 前端普查 grep 全量）：
//   Close/Clear、Minimize、CropSquare、FilterNone、Refresh、Delete、Info、
//   HelpOutline、Inbox、KeyboardReturn、ArrowUp/Downward、Code、ViewModule、
//   Home、LockOpen、KeyboardArrowUp、PushPin
#include "icon_w.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace clashwin::ui {

namespace {

std::string normalize_name(const std::string& mui_name) {
  std::string key;
  key.reserve(mui_name.size());
  for (char c : mui_name) {
    if (c == '_' || c == ' ' || c == '-') continue;
    key += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  }
  for (const char* suffix : {"rounded", "outlined", "sharp", "twotone"}) {
    size_t n = std::strlen(suffix);
    if (key.size() > n && key.compare(key.size() - n, n, suffix) == 0) {
      key.resize(key.size() - n);
      break;
    }
  }
  return key;
}

void add_circle(std::vector<PointF>& out, float cx, float cy, float r, int seg = 32,
                float a0 = 0.0f, float a1 = 6.2831853f) {
  for (int i = 0; i <= seg; ++i) {
    float a = a0 + (a1 - a0) * static_cast<float>(i) / seg;
    out.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
  }
}

// SVG path 扁平化后的折线集合（归一化 0..1，y 向下）。
// 覆盖 UI 实际使用的 9 个图标：close/clear、settings、pushpin、wifi、
// bolt、sort、sortbyalpha、history、minimize。
bool build_vector_paths(const std::string& key, std::vector<std::vector<PointF>>& out) {
  out.clear();
  if (key == "close" || key == "clear") {
    out.push_back({{0.18f, 0.18f}, {0.82f, 0.82f}});
    out.push_back({{0.82f, 0.18f}, {0.18f, 0.82f}});
  } else if (key == "settings") {
    std::vector<PointF> ring;
    add_circle(ring, 0.5f, 0.5f, 0.34f, 36);
    out.push_back(ring);
    std::vector<PointF> inner;
    add_circle(inner, 0.5f, 0.5f, 0.16f, 24);
    out.push_back(inner);
    for (int i = 0; i < 8; ++i) {
      float a = static_cast<float>(i) * 0.7853982f;
      out.push_back({{0.5f + 0.34f * std::cos(a), 0.5f + 0.34f * std::sin(a)},
                     {0.5f + 0.46f * std::cos(a), 0.5f + 0.46f * std::sin(a)}});
    }
  } else if (key == "pushpin") {
    out.push_back({{0.50f, 0.06f}, {0.74f, 0.30f}, {0.50f, 0.46f}, {0.26f, 0.30f},
                   {0.50f, 0.06f}});
    out.push_back({{0.26f, 0.30f}, {0.22f, 0.46f}, {0.78f, 0.46f}, {0.74f, 0.30f}});
    out.push_back({{0.50f, 0.46f}, {0.50f, 0.94f}});
  } else if (key == "wifi") {
    // 三段圆弧（向上）+ 中心点
    for (int i = 1; i <= 3; ++i) {
      std::vector<PointF> arc;
      float r = 0.14f + 0.16f * static_cast<float>(i - 1);
      add_circle(arc, 0.5f, 0.86f, r, 18, 3.665f, 5.760f);  // 210°→330°
      out.push_back(std::move(arc));
    }
    std::vector<PointF> dot;
    add_circle(dot, 0.5f, 0.86f, 0.045f, 12);
    out.push_back(std::move(dot));
  } else if (key == "bolt") {
    out.push_back({{0.60f, 0.05f}, {0.26f, 0.55f}, {0.46f, 0.55f}, {0.38f, 0.95f},
                   {0.74f, 0.42f}, {0.54f, 0.42f}, {0.60f, 0.05f}});
  } else if (key == "sort") {
    out.push_back({{0.16f, 0.24f}, {0.84f, 0.24f}});
    out.push_back({{0.16f, 0.50f}, {0.64f, 0.50f}});
    out.push_back({{0.16f, 0.76f}, {0.44f, 0.76f}});
  } else if (key == "sortbyalpha") {
    out.push_back({{0.30f, 0.88f}, {0.50f, 0.10f}, {0.70f, 0.88f}});
    out.push_back({{0.39f, 0.60f}, {0.61f, 0.60f}});
  } else if (key == "history") {
    std::vector<PointF> ring;
    add_circle(ring, 0.5f, 0.5f, 0.40f, 36);
    out.push_back(ring);
    out.push_back({{0.50f, 0.50f}, {0.50f, 0.22f}});
    out.push_back({{0.50f, 0.50f}, {0.70f, 0.60f}});
  } else if (key == "minimize") {
    out.push_back({{0.20f, 0.50f}, {0.80f, 0.50f}});
  } else {
    return false;
  }
  return true;
}

}  // namespace

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
      {"bolt", 0xE945},         // LightningBolt 近似（原版 ⚡ 组测速）
      {"wifi", 0xE701},         // Wifi（快捷条延迟徽章）
      {"history", 0xE81C},      // History 时钟（排序态 1）
      {"sort", 0xE8CB},         // Sort（排序态 0）
      {"sortbyalpha", 0xE8CC},  // Font 字形（SortByAlpha 近似，字体无对应码点）
      {"play", 0xE768},
      {"pause", 0xE769},
  };
  std::string key = normalize_name(mui_name);
  auto it = kMap.find(key);
  return it != kMap.end() ? it->second : 0;
}

bool IconW::has_vector_path(const std::string& mui_name) {
  std::vector<std::vector<PointF>> tmp;
  return build_vector_paths(normalize_name(mui_name), tmp);
}

void IconW::set_icon(const std::string& mui_name) {
  glyph_ = glyph_for(mui_name);
  name_ = normalize_name(mui_name);
  invalidate();
}

void IconW::set_glyph(wchar_t g) {
  glyph_ = g;
  invalidate();
}

SizeF IconW::on_measure(float max_w, float max_h) {
  (void)max_w; (void)max_h;
  return {box_w > 0 ? box_w : size_, box_h > 0 ? box_h : size_};
}

void IconW::on_paint(IRenderTarget& rt) {
  // 优先级 1：内置 SVG path（折线）——不依赖 Segoe Fluent/MDL2 是否存在
  std::vector<std::vector<PointF>> paths;
  if (!name_.empty() && build_vector_paths(name_, paths)) {
    float w = bounds().w, h = bounds().h;
    float s = w < h ? w : h;
    float side = s * 0.80f;
    float ox = (w - side) * 0.5f, oy = (h - side) * 0.5f;
    Pen pen;
    pen.color = color_;
    pen.width = size_ / 12.0f > 1.0f ? size_ / 12.0f : 1.0f;
    std::vector<PointF> pts;
    auto emit = [&](const Pen& use_pen, float dx, float dy) {
      for (const auto& poly : paths) {
        pts.clear();
        pts.reserve(poly.size());
        for (const auto& p : poly) {
          float x = ox + p.x * side, y = oy + p.y * side;
          if (rotate_deg != 0.0f) {
            // src: _layout.tsx:1362-1364 transform: rotate(45deg)（绕盒中心）
            const float cx = w * 0.5f, cy = h * 0.5f;
            const float rad = rotate_deg * 3.14159265f / 180.0f;
            const float ca = std::cos(rad), sa = std::sin(rad);
            const float rx = x - cx, ry = y - cy;
            x = cx + rx * ca - ry * sa;
            y = cy + rx * sa + ry * ca;
          }
          pts.push_back({x + dx, y + dy});
        }
        if (pts.size() >= 2)
          rt.draw_polyline(pts.data(), static_cast<int>(pts.size()), use_pen);
      }
    };
    if (glow_) {
      // src: _layout.tsx:1359-1361 drop-shadow(0 0 3px rgba(255,59,48,.85))
      //   + drop-shadow(0 1px 1px rgba(255,255,255,.45))；
      //   无高斯模糊核 → 8 向 2px/3px 环形重绘近似
      Pen g; g.color = glow_color_; g.width = pen.width;
      for (int k = 0; k < 8; ++k) {
        const float a = k * 3.14159265f / 4.0f;
        emit(g, std::cos(a) * 2.5f, std::sin(a) * 2.5f);
      }
      Pen w2; w2.color = Color4f::hex(0xFFFFFF, 0.45f); w2.width = pen.width;
      emit(w2, 0.0f, 1.0f);
    }
    emit(pen, 0.0f, 0.0f);
    return;
  }
  // 优先级 2：字体字形（未列入矢量表的图标保持原行为）
  if (!glyph_) return;
  TextStyle ts;
  ts.font_family = font_fallback_ ? L"Segoe MDL2 Assets" : L"Segoe Fluent Icons";
  ts.size = size_;
  ts.color = color_;
  ts.align_h = AlignH::Center;
  ts.align_v = AlignV::Center;
  rt.draw_text(std::wstring(1, glyph_), {0, 0, bounds().w, bounds().h}, ts);
}

bool IconW::on_mouse(const MouseEvent& e) {
  if (!on_click) return false;
  switch (e.type) {
    case MouseEventType::Enter: hover_ = true; invalidate(); return true;
    case MouseEventType::Leave: hover_ = false; pressed_ = false; invalidate(); return true;
    case MouseEventType::Down: pressed_ = true; invalidate(); return true;
    case MouseEventType::Up:
      // 【CLICKFIX】e.pos 局部坐标 vs 局部矩形（原 bounds() 父坐标致点击永不触发）
      if (pressed_ && RectF{0, 0, bounds().w, bounds().h}.contains(e.pos.x, e.pos.y) &&
          on_click)
        on_click();
      pressed_ = false;
      invalidate();
      return true;
    default: return false;
  }
}

}  // namespace clashwin::ui

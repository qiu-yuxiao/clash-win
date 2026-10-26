// 快捷条实现（T13）。
#include "quick_bar.h"

#include <algorithm>

#include "../theme/theme.h"

namespace clashwin::ui::scene {

namespace {
inline const wchar_t* takeover_label(int i) {
  static const wchar_t* kLabels[] = {L"MAN", L"SYS", L"TUN"};
  return kLabels[i];
}
}  // namespace

// ---------- QuickBar ----------
QuickBar::QuickBar(Widget* parent) : Row(parent) {
  set_padding(8, 0, 4, 0);  // px 1(8px) 左、右贴齿轮
  spacing = 4;

  // 左：当前节点名（12px primary 色、单行省略；flex 撑满）
  auto label = std::make_unique<Label>();
  node_label_ = label.get();
  node_label_->style.size = 12;
  node_label_->style.ellipsis = true;
  node_label_->flex_weight = 1;
  add_child(std::move(label));

  // TEST 按钮（描边胶囊，11px；← 截图 TEST 描边胶囊）
  auto test = std::make_unique<Button>(L"TEST");
  test->themed_ = true;
  test->text_style.size = 11;
  test->on_click = [this] {
    if (on_test_group) on_test_group();
  };
  add_child(std::move(test));

  // 图钉（置顶窗口 T19）
  auto pin = std::make_unique<IconW>();
  pin->set_icon("push_pin");
  pin->size_ = 14;
  pin->set_padding(4, 0, 4, 0);
  pin->on_click = [this] {
    if (on_toggle_pin) on_toggle_pin();
  };
  add_child(std::move(pin));

  // 齿轮（设置抽屉 T17 占位）
  auto gear = std::make_unique<IconW>();
  gear->set_icon("settings");
  gear->size_ = 14;
  gear->set_padding(4, 0, 4, 0);
  gear->on_click = [this] {
    if (on_open_settings) on_open_settings();
  };
  add_child(std::move(gear));
}

QuickBar::~QuickBar() = default;

void QuickBar::set_node_text(const std::wstring& text) {
  node_label_->set_text(text);
}

void QuickBar::on_paint(IRenderTarget& rt) {
  const auto& svc = theme::ThemeService::instance();
  rt.fill_rect(bounds(), Brush{svc.vars().background_color});
  rt.fill_rect({bounds().x, bounds().bottom() - 1, bounds().w, 1},
               Brush{svc.vars().divider_color});
  // 节点名 primary 色（← 截图橙色节点名）
  node_label_->style.color = svc.primary();
}

// ---------- TakeoverSegmented ----------
TakeoverSegmented::~TakeoverSegmented() = default;

void TakeoverSegmented::set_mode(const std::string& m) {
  if (mode_ == m) return;
  mode_ = m;
  invalidate();
}

SizeF TakeoverSegmented::on_measure(float max_w, float max_h) {
  (void)max_w; (void)max_h;
  ITextEngine* e = text_engine();
  float per = 0, text_h = 0;
  if (e) {
    for (int i = 0; i < 3; ++i) {
      SizeF s = e->measure_text(takeover_label(i), TextStyle{}, 0);
      if (s.w > per) per = s.w;
      if (s.h > text_h) text_h = s.h;
    }
  }
  return {3 * (per + 16) + 8, std::max(24.0f, text_h + 6)};
}

int TakeoverSegmented::hit_segment(const PointF& local) const {
  float seg_w = bounds().w / 3.0f;
  int idx = static_cast<int>(local.x / seg_w);
  return idx >= 0 && idx < 3 ? idx : -1;
}

bool TakeoverSegmented::on_mouse(const MouseEvent& e) {
  if (e.type == MouseEventType::Move) {
    int idx = hit_segment(e.pos);
    if (hover_ != idx) {
      hover_ = idx;
      invalidate();
    }
    return true;
  }
  if (e.type == MouseEventType::Down) {
    pressed_ = hit_segment(e.pos);
    return true;
  }
  if (e.type == MouseEventType::Up) {
    int idx = hit_segment(e.pos);
    pressed_ = -1;
    if (idx >= 0 && idx < 3) {
      std::string next = takeover_label(idx) == std::wstring(L"MAN")
                             ? "MAN"
                             : (takeover_label(idx) == std::wstring(L"SYS")
                                    ? "SYS"
                                    : "TUN");
      if (next == mode_) return true;
      // ← 失败回弹：apply 失败不改本地状态 + 通知
      if (!apply || apply(next)) {
        mode_ = next;
        invalidate();
      } else if (on_apply_failed) {
        on_apply_failed(next);
      }
      return true;
    }
  }
  return false;
}

void TakeoverSegmented::on_paint(IRenderTarget& rt) {
  auto p = theme::ThemeService::instance().segmented_paint();
  RectF r = bounds();
  Brush bg{p.container_bg};
  rt.round_rect(r, 6, {p.container_border, 1}, &bg);
  float seg_w = r.w / 3.0f;
  static const wchar_t* kLabels[] = {L"MAN", L"SYS", L"TUN"};
  for (int i = 0; i < 3; ++i) {
    RectF seg{r.x + seg_w * i, r.y, seg_w, r.h};
    bool sel = takeover_label(i) == std::wstring(L"MAN")
                   ? mode_ == "MAN"
                   : (takeover_label(i) == std::wstring(L"SYS")
                          ? mode_ == "SYS"
                          : mode_ == "TUN");
    if (sel) {
      Brush ab{p.active_bg};
      rt.round_rect({seg.x + 2, seg.y + 2, seg.w - 4, seg.h - 4},
                    p.active_radius, {p.active_border, 1}, &ab);
    }
    TextStyle ts;
    ts.size = 12;
    ts.color = sel ? p.active_text : p.text;
    ts.weight = sel ? static_cast<int>(Weight::Bold)
                    : static_cast<int>(Weight::Normal);
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    ts.ellipsis = true;
    rt.draw_text(kLabels[i], seg, ts);
  }
}

}  // namespace clashwin::ui::scene

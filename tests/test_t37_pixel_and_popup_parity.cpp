// T37 回归测试：1:1 像素级对齐、顶层跨窗 Tooltip、窄窗弹窗/菜单防裁切与悬停生命周期。
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "test_ui_helper.h"
#include "test_util.h"

#include "i18n/i18n.h"
#include "ui/container.h"
#include "ui/i18n_text.h"
#include "ui/context_menu.h"
#include "ui/scenes/node_list.h"
#include "ui/scenes/profile_import_card.h"
#include "ui/scenes/quick_bar.h"
#include "ui/scenes/settings_drawer.h"
#include "ui/scenes/utility_modals.h"
#include "ui/segmented.h"
#include "ui/switch_w.h"
#include "ui/theme/theme.h"
#include "ui/tooltip_w.h"

using namespace clashwin;
using namespace clashwin::ui;
using namespace ui_test;

namespace {

// 在纯色背景上渲染单个控件并扫描非背景像素的底边 y（物理像素转逻辑 DIP）
float scan_ink_bottom_dip(Widget& w, const RectF& box, const Color4f& bg, float dpi) {
  const float scale = dpi / 96.0f;
  const int pw = std::max(8, static_cast<int>(std::ceil((box.x + box.w + 8.0f) * scale)));
  const int ph = std::max(8, static_cast<int>(std::ceil((box.y + box.h + 8.0f) * scale)));
  make_offscreen(pw, ph, dpi);
  w.layout_tree(box);
  rt().begin_frame();
  rt().clear(bg);
  w.paint_all(rt());
  rt().end_frame();

  std::vector<uint8_t> buf;
  int rw = 0, rh = 0;
  if (!rt().read_pixels(buf, rw, rh)) return -1.0f;

  const uint8_t br = static_cast<uint8_t>(std::round(bg.r * 255.0f));
  const uint8_t bg_g = static_cast<uint8_t>(std::round(bg.g * 255.0f));
  const uint8_t bb = static_cast<uint8_t>(std::round(bg.b * 255.0f));

  const int x0 = std::max(0, static_cast<int>(std::floor(box.x * scale)));
  const int x1 = std::min(rw - 1, static_cast<int>(std::ceil(box.right() * scale)));
  const int y0 = std::max(0, static_cast<int>(std::floor(box.y * scale)));
  const int y1 = std::min(rh - 1, static_cast<int>(std::ceil(box.bottom() * scale)));

  int max_y = -1;
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const uint8_t* p = buf.data() + (static_cast<size_t>(y) * rw + x) * 4u;
      const int diff = std::abs(static_cast<int>(p[2]) - static_cast<int>(br)) +
                       std::abs(static_cast<int>(p[1]) - static_cast<int>(bg_g)) +
                       std::abs(static_cast<int>(p[0]) - static_cast<int>(bb));
      if (diff > 28) {
        max_y = y;
      }
    }
  }
  return max_y >= 0 ? (static_cast<float>(max_y + 1) / scale) : -1.0f;
}

}  // namespace

// 1) BasicCard 四个开关与标签文字底沿像素级对齐（跨语言 zh/en + 跨 6 套皮肤）+ 消除圆外“直耳”
UT_TEST(t37_basic_card_switches_and_labels_bottom_aligned_pixels) {
  ensure_i18n();
  auto& i18n_svc = i18n::I18n::instance();
  const std::string prev_lang = i18n_svc.language();
  auto& svc = theme::ThemeService::instance();
  const theme::SkinId skins[] = {
      theme::SkinId::Retro3d,     theme::SkinId::Original,
      theme::SkinId::ModernFlat,  theme::SkinId::FrostedGlass,
      theme::SkinId::Cyberpunk,   theme::SkinId::Monochrome};
  const char* langs[] = {"zh", "en"};

  for (const char* lang : langs) {
    i18n_svc.set_language(lang);
    for (theme::SkinId sk : skins) {
      svc.init(sk, theme::ThemeMode::Light);
      make_offscreen(271, 680, 144.0f);
      scene::SettingsDrawer drawer;
      drawer.layout_tree({0, 0, 271, 680});
      drawer.set_open(true);

      const auto* basic = drawer.basic_card();
      UT_CHECK(basic != nullptr);
      if (!basic) continue;

      // 所有标签必须为 AlignV::Center（严禁退化回 AlignV::Top 导致上飘 4px）
      for (int i = 0; i < 4; ++i) {
        UT_CHECK(basic->labels_[i] != nullptr);
        if (basic->labels_[i]) {
          UT_CHECK(basic->labels_[i]->style.align_v == AlignV::Center);
        }
      }
      UT_CHECK(basic->port_label_ && basic->port_label_->style.align_v == AlignV::Center);

      SwitchW* sws[4] = {basic->sw_auto_launch_, basic->sw_allow_lan_,
                         basic->sw_silent_start_, basic->sw_allow_ipv6_};
      const auto sp = svc.switch_paint();
      for (int i = 0; i < 4; ++i) {
        UT_CHECK(sws[i] != nullptr);
        if (!sws[i] || !basic->labels_[i]) continue;
        // 尺寸按皮肤自身尺寸 × 0.85（cyberpunk 18×10 → 15.3×8.5；其余 28×14 → 23.8×11.9）
        UT_CHECK(near_f(sws[i]->bounds().w, sp.track_w * 0.85f, 0.25f));
        UT_CHECK(near_f(sws[i]->bounds().h, sp.track_h * 0.85f, 0.25f));

        // 离屏逐像素扫描该标签的真实字面墨迹底沿（剔除 y/g/p/q/j 下伸尾以测主基线，并单独验证原串）
        Label lb_probe;
        lb_probe.style = basic->labels_[i]->style;
        lb_probe.style.color = Color4f::hex(0x111111);
        std::wstring base_txt = basic->labels_[i]->text;
        for (wchar_t& ch : base_txt) {
          if (ch == L'g' || ch == L'j' || ch == L'p' || ch == L'q' || ch == L'y') {
            ch = L'a';
          }
        }
        lb_probe.set_text(base_txt);
        const RectF lb_box{0.0f, 0.0f, basic->labels_[i]->bounds().w,
                           basic->labels_[i]->bounds().h};
        const float lb_base_bot =
            scan_ink_bottom_dip(lb_probe, lb_box, Color4f::hex(0xFFFFFF), 144.0f);

        const float sw_body_bot =
            (sws[i]->bounds().y - basic->labels_[i]->bounds().y) +
            std::max(sws[i]->bounds().h,
                     0.5f * (sws[i]->bounds().h + sp.thumb_d * 0.85f));
        UT_CHECK(lb_base_bot > 10.0f);
        // 跨 OS 字体 / 144 DPI 灰度抗锯齿边缘（1~2 物理像素 ≈ 0.67~1.33 DIP）下，主基线与开关底沿误差 <= 1.5 DIP
        UT_CHECK(std::fabs(lb_base_bot - sw_body_bot) <= 1.5f);
      }
    }
  }
  i18n_svc.set_language(prev_lang);

  // 验证 Retro3d 小开关旋钮顶部/底部高光线不越出圆形边界形成“直耳”
  svc.init(theme::SkinId::Retro3d, theme::ThemeMode::Dark);
  svc.set_factors(0.0f, 0.0f);  // 关掉阴影与外发光，纯测几何边界
  make_offscreen(80, 60, 144.0f);
  SwitchW sw;
  sw.set_checked(false, false);
  const RectF sw_box{10.0f, 10.0f, 23.8f, 11.9f};
  sw.layout_tree(sw_box);
  rt().begin_frame();
  rt().clear(Color4f::hex(0x000000));
  sw.paint_all(rt());
  rt().end_frame();
  // 轨道左上角圆弧外侧 (x = 10.3, y = 10.3) 距圆心 (15.95, 15.95) 距离 = sqrt(5.65^2 * 2) = 7.99 > 5.95，必须保持纯黑背景
  const Px corner_outside = pixel(static_cast<int>(10.3f * 1.5f),
                                  static_cast<int>(10.3f * 1.5f));
  UT_CHECK(corner_outside.r < 25 && corner_outside.g < 25 && corner_outside.b < 25);

  svc.init(theme::SkinId::Retro3d, theme::ThemeMode::Light);
  svc.set_factors(0.3f, 1.0f);
}

// 2) TooltipHost：271px 窄窗右缘不裁切、Stack relayout 不冲掉 bounds、顶层分层窗口跨出 271px
UT_TEST(t37_tooltip_host_no_clipping_and_layered_popup) {
  make_offscreen(271, 680, 96.0f);

  Stack root;
  auto btn = std::make_unique<Widget>();
  Widget* btn_p = btn.get();
  btn_p->set_tooltip_text(L"规则分流：国内直连，常见被墙网站自动走代理");
  root.add_child(std::move(btn));

  auto tip = std::make_unique<TooltipHost>();
  TooltipHost* tip_p = tip.get();
  root.add_child(std::move(tip));

  root.layout_tree({0, 0, 271, 680});
  btn_p->set_bounds({235, 32, 28, 28});

  // 离屏模式（无 HWND）：右缘按钮提示不得越出 271px 画布，且 relayout 不得把 tip_bounds 覆写成 271×680
  tip_p->tick_now(0);
  tip_p->hover(btn_p, {250.0f, 46.0f});
  tip_p->tick_now(TooltipHost::kShowDelayMs + 10);
  UT_CHECK(tip_p->showing());
  root.relayout();
  btn_p->set_bounds({235, 32, 28, 28});
  tip_p->tick_now(TooltipHost::kShowDelayMs + 20);
  const RectF tb = tip_p->tip_bounds();
  UT_CHECK(tb.w > 40.0f && tb.w <= 271.0f);
  UT_CHECK(tb.h > 16.0f && tb.h < 200.0f);
  UT_CHECK(tb.x >= 0.0f && tb.right() <= 271.0f);

  // 实机顶层分层窗口模式（绑定可见测试主窗 HWND）：宽提示允许突破 271px 窄窗宽度完整展示
  tip_p->leave();
  HWND host_hwnd = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"", WS_POPUP,
      200, 200, 271, 680, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  UT_CHECK(host_hwnd != nullptr);
  if (host_hwnd) {
    ShowWindow(host_hwnd, SW_SHOWNOACTIVATE);
    tip_p->bind_owner_hwnd(host_hwnd);
    tip_p->tick_now(1000);
    tip_p->hover(btn_p, {250.0f, 46.0f});
    tip_p->tick_now(1000 + TooltipHost::kShowDelayMs + 10);
    UT_CHECK(tip_p->showing());
    UT_CHECK(tip_p->popup_hwnd() != nullptr);
    UT_CHECK(IsWindowVisible(tip_p->popup_hwnd()) != FALSE);
    const RECT sr = tip_p->popup_screen_rect();
    const int popup_w = sr.right - sr.left;
    UT_CHECK(popup_w > 120);  // 完整单行宽度，未被 271px 窗口右缘截成一半

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromWindow(host_hwnd, MONITOR_DEFAULTTONEAREST);
    if (mon && GetMonitorInfoW(mon, &mi)) {
      UT_CHECK(sr.left >= mi.rcWork.left);
      UT_CHECK(sr.right <= mi.rcWork.right);
    }

    tip_p->leave();
    UT_CHECK(!tip_p->showing());
    UT_CHECK(IsWindowVisible(tip_p->popup_hwnd()) == FALSE);
    tip_p->bind_owner_hwnd(nullptr);
    DestroyWindow(host_hwnd);
  }
}

// 3) 补齐缺失的 Tooltip 与鼠标移出悬停清理 clear_mouse_hover()
UT_TEST(t37_missing_tooltips_and_mouse_leave_cleanup) {
  make_offscreen(271, 680, 96.0f);

  // ⚡ 批量测速按钮必须带 delayCheck 提示
  scene::NodeList nl;
  nl.layout_tree({0, 0, 271, 400});
  UT_CHECK(nl.bolt_button() != nullptr);
  if (nl.bolt_button()) {
    UT_CHECK(!nl.bolt_button()->tooltip_text().empty());
  }

  // QuickBar 仅在悬停节点名时返回 rotateNodeTooltip，悬停右侧齿轮区不误弹节点轮换提示
  scene::QuickBar qb;
  qb.set_node_text(L"HK-Premium-01");
  qb.set_badge(42, true);
  qb.layout_tree({0, 0, 271, 30});
  UT_CHECK(qb.tooltip_text().empty());

  // NodeRowWidget：左协议列显示协议，中名称列显示节点名（选中附当前节点），
  // 右延迟列非预设节点显示单点测速提示（BUG-357），预设节点右延迟列为空
  scene::NodeRowWidget row;
  row.bind("Trojan", "Tokyo-IPLC-01", 35, true);
  row.layout_tree({0, 0, 255, 26});
  MouseEvent mv_proto{MouseEventType::Move, {20.0f, 12.0f}};
  row.on_mouse(mv_proto);
  UT_CHECK(row.tooltip_text() == L"Trojan");
  MouseEvent mv_name{MouseEventType::Move, {100.0f, 12.0f}};
  row.on_mouse(mv_name);
  UT_CHECK(row.tooltip_text().find(L"Tokyo-IPLC-01") != std::wstring::npos);
  MouseEvent mv_delay{MouseEventType::Move, {230.0f, 12.0f}};
  row.on_mouse(mv_delay);
  UT_CHECK(!row.tooltip_text().empty());
  scene::NodeRowWidget preset_row;
  preset_row.bind("Direct", "DIRECT", -1, false);
  preset_row.layout_tree({0, 0, 255, 26});
  preset_row.on_mouse(mv_delay);
  UT_CHECK(preset_row.tooltip_text().empty());

  // clear_mouse_hover() 必须触发 Leave 并清空全局 hovered_widget()
  Widget root;
  root.set_bounds({0, 0, 200, 100});
  auto sw = std::make_unique<SwitchW>();
  SwitchW* sw_p = sw.get();
  sw->set_bounds({10, 10, 28, 14});
  root.add_child(std::move(sw));
  move_to(&root, 20.0f, 16.0f);
  UT_CHECK(hovered_widget() == sw_p);
  clear_mouse_hover();
  UT_CHECK(hovered_widget() == nullptr);
}

// 4) ContextMenu / MenuList：13 语言菜单在 271×680 抽屉内不越顶裁切，滚轮可滚到底部点击末项
UT_TEST(t37_context_menu_clamping_and_wheel_scroll) {
  make_offscreen(271, 680, 96.0f);

  Widget host;
  host.set_bounds({0, 0, 271, 550});
  auto cm = std::make_unique<ContextMenu>();
  ContextMenu* cm_p = cm.get();
  cm->set_bounds({0, 0, 271, 550});
  host.add_child(std::move(cm));

  cm_p->style_.item_h = 48.0f;
  cm_p->style_.pad_v = 2.0f;
  std::vector<MenuItem> items(13);
  for (int i = 0; i < 13; ++i) {
    items[static_cast<size_t>(i)].label = L"Lang-" + std::to_wstring(i);
  }
  items[0].selected = true;

  // 模拟旧代码锚点 y = -110：打开后必须钳在 [4, 546] 内
  cm_p->open(items, {16.0f, -110.0f});
  const RectF mb = cm_p->menu_rect();
  UT_CHECK(mb.y >= 4.0f);
  UT_CHECK(mb.bottom() <= 546.5f);
  UT_CHECK(mb.x >= 4.0f && mb.right() <= 267.5f);

  int picked = -1;
  cm_p->on_select = [&](int idx) { picked = idx; };
  // 向下滚动到底并点击最底部菜单项（第 12 项）
  for (int step = 0; step < 6; ++step) {
    wheel_at(&host, mb.x + 30.0f, mb.y + 30.0f, -120);
  }
  click(&host, mb.x + 30.0f, mb.bottom() - 16.0f);
  UT_CHECK_EQ(picked, 12);
}

// 5) 271px 窄窗下 ClientUpdateModal / CoreUpdateModal / EditProfileModal 卡片与按钮全部在视口内
UT_TEST(t37_narrow_window_modals_fit_within_271px) {
  make_offscreen(271, 680, 96.0f);

  {
    scene::ClientUpdateModal m;
    m.open("v0.2.6", "v0.2.7", "Fix parity");
    m.layout_tree({0, 0, 271, 680});
    UT_CHECK(m.card_rect().x >= 0.0f);
    UT_CHECK(m.card_rect().right() <= 271.0f);
    bool closed = false;
    m.on_dismiss = [&] { closed = true; };
    // 点击卡片右下关闭钮区域（在 271px 内可点击，不再悬在 x=330 窗外）
    const float close_x = m.card_rect().right() - 16.0f - 82.0f - 8.0f - 38.0f;
    const float btn_y = m.card_rect().bottom() - 29.0f;
    UT_CHECK(close_x > m.card_rect().x && close_x < 271.0f);
    MouseEvent up{MouseEventType::Up, {close_x, btn_y}, kMouseLeft};
    m.on_mouse(up);
    UT_CHECK(closed);
  }

  {
    scene::CoreUpdateModal m;
    m.open("v1.19.27", "v1.19.28");
    m.layout_tree({0, 0, 271, 680});
    UT_CHECK(m.card_rect().x >= 0.0f);
    UT_CHECK(m.card_rect().right() <= 271.0f);
  }

  {
    scene::EditProfileModal m;
    m.open("Sub", "https://example.com", 60, false);
    m.layout_tree({0, 0, 271, 680});
    UT_CHECK(m.card_rect().x >= 0.0f);
    UT_CHECK(m.card_rect().right() <= 271.0f);
  }
}

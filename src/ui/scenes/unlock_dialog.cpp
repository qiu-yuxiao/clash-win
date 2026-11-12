#include "unlock_dialog.h"

#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#include "events/notice.h"
#include "system/unlock_checker.h"
#include "ui/theme/theme.h"
#include "ui/ui_postbox.h"
#include "util/utf.h"
#include "ui/i18n_text.h"  // F-D01

namespace clashwin::ui::scene {

using theme::ThemeService;

namespace {
// 顶部标题/动作区与卡片（src: pages/unlock.tsx:351-374 BasePage header + :388-517 卡片）
constexpr float kPadX = 14.0f;          // 网格左右 breathing（Card mx 由 12 gap 控制）
constexpr float kGridGap = 12.0f;       // :388 spacing=1.5 → 12px
constexpr float kCardH = 93.0f;         // 卡片内容高：p10.4 + 名称22 + Chip22 + 虚线 + 时间
constexpr float kCardPad = 10.4f;       // :412 p: 1.3 → 10.4px
constexpr float kCardRadius = 16.0f;    // :395 borderRadius: 2 → 16px
constexpr float kStatusBorder = 4.0f;   // :396 borderLeft: 4px solid
constexpr float kTestBtn = 32.0f;       // :441-443 32×32 圆形按钮
constexpr float kChipH = 22.0f;
constexpr float kCloseSize = 24.0f;
constexpr float kRetestW = 88.0f;       // header contained small 按钮（:356-372）
constexpr float kRetestH = 26.0f;
constexpr float kHeaderH = 44.0f;       // 标题行 + 间距

// MUI palette.divider（组件级，非 CSS --divider-color）：light rgba(0,0,0,.12) /
//   dark rgba(255,255,255,.12)（src: MUI 默认 palette；:393 Card outlined 边框）
Color4f ui_divider(bool light) {
  return light ? Color4f::hex(0x000000, 0.12f) : Color4f::hex(0xFFFFFF, 0.12f);
}

Color4f status_color(const std::string& status, const theme::Palette& pal,
                     bool light) {
  // src: pages/unlock.tsx:340-347 getStatusBorderColor
  if (status == "Yes") return pal.success_color;
  if (status == "No") return pal.error_color;
  if (status == "Soon") return pal.warning_color;
  if (status.find("Failed") != std::string::npos) return pal.error_color;
  if (status == "Completed") return pal.info_color;
  return ui_divider(light);
}

std::wstring status_label(const std::string& status) {
  // src: pages/unlock.tsx:41-52 STATUS_LABEL_KEYS
  // src: locales/zh/tests.json:26-38 状态文案（unlock.tsx:41-52 STATUS_LABEL_KEYS）
  if (status == "Pending") return tr("tests.statuses.test.pending");
  if (status == "Yes") return tr("tests.statuses.test.yes");
  if (status == "No") return tr("tests.statuses.test.no");
  if (status == "Failed") return tr("tests.statuses.test.failed");
  if (status == "Completed") return tr("tests.statuses.test.completed");
  if (status == "Disallowed ISP") return tr("tests.statuses.test.disallowedIsp");
  if (status == "Originals Only") return tr("tests.statuses.test.originalsOnly");
  if (status == "No (IP Banned By Disney+)") return tr("tests.statuses.test.noDisney");
  if (status == "Unsupported Country/Region") return tr("tests.statuses.test.unsupportedRegion");
  if (status == "Failed (Network Connection)") return tr("tests.statuses.test.failedNetwork");
  return utf::utf8_to_utf16(status);
}
}  // namespace

UnlockDialog::UnlockDialog(Widget* parent) : Widget(parent) {
  set_z_order(960);
  items_ = system::UnlockChecker::instance().get_default_items();
}

void UnlockDialog::open() {
  open_ = true;
  hover_close_x_ = false;
  hover_retest_ = false;
  hover_card_ = -1;
  reload_from_cache();
  invalidate();
}

void UnlockDialog::close() {
  if (!open_) return;
  open_ = false;
  invalidate();
  if (on_dismiss) on_dismiss();
}

void UnlockDialog::set_items(std::vector<config::unlock_cache::UnlockItem> items) {
  items_ = std::move(items);
  invalidate();
}

void UnlockDialog::reload_from_cache() {
  items_ = system::UnlockChecker::instance().load_cached_items();
}

void UnlockDialog::start_check() {
  if (is_checking_) return;
  is_checking_ = true;
  for (auto& it : items_) it.status = "Pending";
  invalidate();
  std::thread([this] {
    // ← unlock.tsx:232-247 invokeWithTimeout + :249-267 checkAllMedia：
    //   15s（UNLOCK_TIMEOUT_MS，services/delay.ts:36）未完成或抛错 →
    //   setIsCheckingAll(false) + showNotice.error(detectionTimeout, err)
    using namespace std::chrono;
    auto fut = system::UnlockChecker::instance().check_all_async();
    std::vector<config::unlock_cache::UnlockItem> results;
    bool ok = true;
    try {
      if (fut.wait_for(milliseconds(15000)) != std::future_status::ready) {
        ok = false;
      } else {
        results = fut.get();
      }
    } catch (...) {
      ok = false;
    }
    // 结果只能回 UI 线程改控件树（后台线程直接改 items_ 会与绘制竞争）
    UiPostbox::instance().post([this, ok, results] {
      is_checking_ = false;
      if (ok) {
        items_ = results;
      } else {
        // ← :264 showNotice.error('tests.unlock.page.messages.detectionTimeout', err)
        events::NoticeService::instance().push(
            "error", tr8("tests.unlock.page.messages.detectionTimeout"));
      }
      invalidate();
    });
  }).detach();
}

void UnlockDialog::on_arrange(const RectF& area) {
  set_bounds(area);
  // 整页模式：Mini pages/unlock.tsx:1206-1219 layout-content padding 20
  // 模态模式：纸张宽 520（Grid md:3 合理容纳宽），backdrop/关闭为承载壳
  const float cw = page_mode_ ? std::max(1.0f, area.w - 40.0f)
                              : std::min(520.0f, area.w - 24.0f);
  const int cols = cw >= 480.0f ? 3 : (cw >= 360.0f ? 2 : 1);
  const int rows =
      std::max(1, (static_cast<int>(items_.size()) + cols - 1) / cols);
  const float grid_w = cw - kPadX * 2.0f;
  const float card_w = (grid_w - kGridGap * (cols - 1)) / cols;
  const float grid_h = rows * kCardH + (rows - 1) * kGridGap;
  const float ch = page_mode_ ? std::max(kHeaderH + 16.0f, area.h - 40.0f)
                              : kHeaderH + grid_h + 16.0f;
  const float cx = page_mode_ ? 20.0f : std::max(0.0f, (area.w - cw) / 2.0f);
  const float cy = page_mode_ ? 20.0f : std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};
  // 整页滚动：卡片区视口 = card 高 - 标题行 - 底部留白
  const float view_h = card_rect_.h - kHeaderH - 16.0f;
  max_scroll_ = std::max(0.0f, grid_h - view_h);
  scroll_ = std::min(std::max(0.0f, scroll_), max_scroll_);

  btn_close_x_rect_ = {cx + cw - kCloseSize - 8.0f, cy + 8.0f, kCloseSize, kCloseSize};
  btn_retest_rect_ = {cx + cw - kCloseSize - 8.0f - 6.0f - kRetestW,
                      cy + 8.0f + (kCloseSize - kRetestH) / 2.0f, kRetestW, kRetestH};

  ctrls_.clear();
  ctrls_.resize(items_.size());
  for (size_t i = 0; i < items_.size(); ++i) {
    const int r = static_cast<int>(i) / cols;
    const int c = static_cast<int>(i) % cols;
    Ctrl& ct = ctrls_[i];
    // 整页模式：网格随 scroll_（在 on_arrange 内已计算并钳制）上移
    ct.card = {cx + kPadX + c * (card_w + kGridGap),
               cy + kHeaderH - scroll_ + r * (kCardH + kGridGap), card_w, kCardH};
    // 测试按钮 32×32 圆形（:441-443），右上角
    ct.retest = {ct.card.x + card_w - kCardPad - kTestBtn,
                 ct.card.y + kCardPad, kTestBtn, kTestBtn};
    // Chip 行（:471-489）：状态 Chip + 地区 outlined Chip
    ct.chip = {ct.card.x + kCardPad, ct.card.y + kCardPad + 24.0f, 64.0f, kChipH};
    ct.region = {ct.chip.x + ct.chip.w + 8.0f, ct.chip.y, 44.0f, kChipH};
  }
}

void UnlockDialog::on_paint(IRenderTarget& rt) {
  if (!open_) return;
  const bool light = ThemeService::instance().is_light();
  const auto& pal = ThemeService::instance().palette();

  // 整页模式铺满窗口底（src: _layout.tsx:1194-1200 背景），无模态遮罩
  if (page_mode_) {
    rt.fill_rect({0, 0, bounds().w, bounds().h},
                 Brush{light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x181A1B)});
  } else {
    // 豁免: ClashWin 以模态承载解锁检测（Mini 为 /unlock 整页路由，无弹窗）；
    //   遮罩沿用 MUI Backdrop 默认 rgba(0,0,0,.5)（Backdrop.js:48）。
    rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{Color4f(0, 0, 0, 0.5f)});
  }

  // 纸面：theme-panel 底色（源码 Card backgroundColor var(--theme-panel-bg)，
  //   fallback light #ffffff / dark #282a36，:397-400），radius 16（:395）
  Brush paper{light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x282A36)};
  rt.round_rect(card_rect_, kCardRadius,
                Pen{ui_divider(light), 1.0f}, &paper);  // Card variant outlined

  // 标题（BasePage title，MUI h6 14px 600）
  TextStyle title;
  title.size = 15;
  title.weight = 600;
  title.color = pal.primary_text;
  title.align_v = AlignV::Center;
  // src: locales/zh/tests.json:50 「"解锁测试"」
  rt.draw_text(tr("tests.unlock.page.title"),
               {card_rect_.x + kPadX, card_rect_.y + 8.0f,
                card_rect_.w - kPadX * 2.0f - kRetestW - kCloseSize - 18.0f, 22.0f},
               title);

  // 「测试全部」contained primary small（:356-372；引擎 Button 平涂，豁免渐变）
  Brush rb{is_checking_ ? Color4f::hex(0x9E9E9E) : ThemeService::instance().primary()};
  rt.round_rect(btn_retest_rect_, 6.0f, {}, &rb);
  TextStyle rts;
  rts.size = 12;
  rts.weight = 500;
  rts.color = Color4f::hex(0xFFFFFF);
  rts.align_h = AlignH::Center;
  rts.align_v = AlignV::Center;
  // src: locales/zh/tests.json:43 「"测试中..."」
  rt.draw_text(is_checking_ ? tr("tests.unlock.page.actions.testing") : tr("tests.page.actions.testAll"), btn_retest_rect_, rts);

  // 关闭 ✕（豁免: ClashWin 模态壳，Mini 页面无关闭按钮）
  TextStyle xts;
  xts.size = 12;
  xts.color = pal.secondary_text;
  xts.align_h = AlignH::Center;
  xts.align_v = AlignV::Center;
  rt.draw_text(L"✕", btn_close_x_rect_, xts);

  // 卡片网格（:388-517）；整页模式裁剪到标题行下方视口（滚动）
  if (page_mode_) {
    rt.push_clip({card_rect_.x + 1.0f, card_rect_.y + kHeaderH,
                  card_rect_.w - 2.0f, card_rect_.h - kHeaderH - 2.0f});
  }
  if (items_.empty()) {
    // src: unlock.tsx:376-386 unlockItems 为空 → BaseEmpty
    //   height 50% 居中；文案 tests.json:45「暂无解锁测试项目」
    RectF view{card_rect_.x, card_rect_.y + kHeaderH, card_rect_.w,
               (card_rect_.h - kHeaderH) * 0.5f};
    TextStyle ets;
    ets.size = 16.0f;
    Color4f ec = pal.secondary_text;
    ec.a *= 0.75f;
    ets.color = ec;
    ets.align_h = AlignH::Center;
    ets.align_v = AlignV::Center;
    rt.draw_text(tr("tests.unlock.page.empty"), view, ets);
  }
  for (size_t i = 0; i < items_.size() && i < ctrls_.size(); ++i) {
    const auto& it = items_[i];
    const Ctrl& ct = ctrls_[i];
    const Color4f sc = status_color(it.status, pal, light);

    // 悬停：alpha(primary.light/dark, .05)（:403-407，MUI primary.light #42A5F5 / dark #1565C0）
    Brush bg{light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x282A36)};
    if (hover_card_ == static_cast<int>(i)) {
      bg = Brush{light ? Color4f::hex(0x42A5F5, 0.05f)
                       : Color4f::hex(0x1565C0, 0.05f)};
    }
    rt.round_rect(ct.card, kCardRadius, Pen{ui_divider(light), 1.0f}, &bg);
    // borderLeft 4px 状态色（:396）
    rt.fill_rect({ct.card.x, ct.card.y, kStatusBorder, ct.card.h}, Brush{sc});

    // 名称 subtitle1 16px 600 text.primary（:420-429）
    TextStyle name;
    name.size = 16;
    name.weight = 600;
    name.color = pal.primary_text;
    name.align_v = AlignV::Center;
    name.ellipsis = true;
    rt.draw_text(utf::utf8_to_utf16(it.name),
                 {ct.card.x + kCardPad + kStatusBorder, ct.card.y + kCardPad,
                  ct.retest.x - ct.card.x - kCardPad - kStatusBorder - 6.0f, 24.0f},
                 name);

    // 32×32 圆形 outlined primary 测试按钮（:432-458）
    rt.round_rect(ct.retest, kTestBtn / 2.0f,
                  Pen{ThemeService::instance().primary(), 1.0f}, nullptr);
    TextStyle icon;
    icon.size = 16;
    icon.color = ThemeService::instance().primary();
    icon.align_h = AlignH::Center;
    icon.align_v = AlignV::Center;
    rt.draw_text(L"↻", ct.retest, icon);

    // 状态 Chip：filled color=(success/error/warning/info/default)（:471-480）
    const std::string st = it.status;
    Color4f chip_bg = ui_divider(light);
    Color4f chip_fg = pal.secondary_text;
    if (st == "Yes") {
      chip_bg = pal.success_color;
      chip_fg = Color4f::hex(0xFFFFFF);
    } else if (st == "No" || st.find("Failed") != std::string::npos) {
      chip_bg = pal.error_color;
      chip_fg = Color4f::hex(0xFFFFFF);
    } else if (st == "Soon") {
      chip_bg = pal.warning_color;
      chip_fg = Color4f::hex(0xFFFFFF);
    } else if (st == "Completed") {
      chip_bg = pal.info_color;
      chip_fg = Color4f::hex(0xFFFFFF);
    }
    Brush cb{chip_bg};
    rt.round_rect(ct.chip, kChipH / 2.0f, {}, &cb);  // Chip 胶囊
    TextStyle chip_ts;
    chip_ts.size = 11;
    chip_ts.weight = st == "Pending" ? 400 : 600;  // :477-479
    chip_ts.color = chip_fg;
    chip_ts.align_h = AlignH::Center;
    chip_ts.align_v = AlignV::Center;
    rt.draw_text(status_label(st), ct.chip, chip_ts);

    // 地区 Chip：outlined info（:482-489）
    if (it.region && !it.region->empty()) {
      rt.round_rect(ct.region, kChipH / 2.0f,
                    Pen{pal.info_color, 1.0f}, nullptr);
      TextStyle reg;
      reg.size = 11;
      reg.color = pal.info_color;
      reg.align_h = AlignH::Center;
      reg.align_v = AlignV::Center;
      rt.draw_text(utf::utf8_to_utf16(*it.region), ct.region, reg);
    }

    // 虚线分隔（:493-499）+ 检测时间 caption 0.7rem 右对齐（:501-513）
    const float div_y = ct.card.y + ct.card.h - 25.0f;
    Brush dash{ui_divider(light)};
    for (float dx = ct.card.x + kCardPad; dx < ct.card.x + ct.card.w - kCardPad;
         dx += 6.0f) {  // 豁免: page 无虚线图元，以 3px 实/3px 空近似 dashed
      rt.fill_rect({dx, div_y, 3.0f, 1.0f}, dash);
    }
    TextStyle tm;
    tm.size = 11.2;  // 0.7rem
    tm.color = pal.secondary_text;
    tm.align_h = AlignH::Right;
    tm.align_v = AlignV::Center;
    rt.draw_text(utf::utf8_to_utf16(it.check_time && !it.check_time->empty()
                                        ? *it.check_time
                                        : std::string("-- --")),
                 {ct.card.x + kCardPad, div_y + 2.0f,
                  ct.card.w - kCardPad * 2.0f, 16.0f}, tm);
  }
  if (page_mode_) rt.pop_clip();
}

bool UnlockDialog::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  PointF pt = e.pos;

  // 整页滚动（Mini 页面可滚动；模态模式无此项）
  if (page_mode_ && e.type == MouseEventType::Wheel) {
    scroll_ -= e.wheel_delta / 120.0f * 48.0f;
    scroll_ = std::min(std::max(0.0f, scroll_), max_scroll_);
    invalidate();
    return true;
  }

  if (e.type == MouseEventType::Move) {
    const bool hx = btn_close_x_rect_.contains(pt.x, pt.y);
    const bool hr = btn_retest_rect_.contains(pt.x, pt.y);
    int hc = -1;
    for (size_t i = 0; i < ctrls_.size(); ++i) {
      if (ctrls_[i].retest.contains(pt.x, pt.y)) {
        hc = static_cast<int>(i);
        break;
      }
    }
    if (hx != hover_close_x_ || hr != hover_retest_ || hc != hover_card_) {
      hover_close_x_ = hx;
      hover_retest_ = hr;
      hover_card_ = hc;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (btn_close_x_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    if (btn_retest_rect_.contains(pt.x, pt.y)) {
      start_check();
      return true;
    }
    for (size_t i = 0; i < ctrls_.size(); ++i) {
      if (ctrls_[i].retest.contains(pt.x, pt.y)) {
        start_check();
        return true;
      }
    }
    if (!card_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    return true;
  }

  if (e.type == MouseEventType::Down) return true;  // 拦截下层点击
  return true;
}

bool UnlockDialog::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down && (e.key == VK_ESCAPE || e.key == 27)) {
    close();
    return true;
  }
  return false;
}

std::wstring UnlockDialog::a11y_default_name() const {
  return tr("tests.unlock.page.title");
}

size_t UnlockDialog::a11y_virtual_child_count() const {
  const size_t header_count = page_mode_ ? 1u : 2u;
  const size_t card_count = std::min(items_.size(), ctrls_.size());
  return header_count + card_count;
}

A11ySubItem UnlockDialog::a11y_virtual_child(size_t idx) const {
  A11ySubItem sub;
  if (idx == 0) {
    sub.role = A11yRole::Button;
    sub.name = is_checking_ ? tr("tests.unlock.page.actions.testing")
                            : tr("tests.page.actions.testAll");
    sub.local_bounds = btn_retest_rect_;
    sub.can_invoke = !is_checking_;
    sub.enabled = !is_checking_;
    return sub;
  }
  if (!page_mode_ && idx == 1) {
    sub.role = A11yRole::Button;
    sub.name = tr("shared.actions.close");
    sub.local_bounds = btn_close_x_rect_;
    sub.can_invoke = true;
    return sub;
  }
  const size_t hdr = page_mode_ ? 1u : 2u;
  if (idx >= hdr) {
    const size_t ci = idx - hdr;
    if (ci < items_.size() && ci < ctrls_.size()) {
      const auto& it = items_[ci];
      sub.role = A11yRole::Button;
      std::wstring label = utf::utf8_to_utf16(it.name) + L" - " + status_label(it.status);
      if (it.region && !it.region->empty()) {
        label += L" (" + utf::utf8_to_utf16(*it.region) + L")";
      }
      sub.name = label;
      sub.local_bounds = ctrls_[ci].card;
      sub.can_invoke = !is_checking_;
      sub.enabled = !is_checking_;
    }
  }
  return sub;
}

bool UnlockDialog::a11y_invoke_virtual_child(size_t idx) {
  if (!open_ || !enabled()) return false;
  if (idx == 0) {
    if (is_checking_) return false;
    start_check();
    return true;
  }
  if (!page_mode_ && idx == 1) {
    close();
    return true;
  }
  const size_t hdr = page_mode_ ? 1u : 2u;
  if (idx >= hdr && (idx - hdr) < items_.size() && !is_checking_) {
    start_check();
    return true;
  }
  return false;
}

}  // namespace clashwin::ui::scene

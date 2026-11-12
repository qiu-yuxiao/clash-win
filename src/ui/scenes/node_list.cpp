// 节点列表实现（T13）：过滤条 + 虚拟滚动 + 行卡绘制（对照 proxy-item.tsx）。
#include "node_list.h"
#include "ui/ui_postbox.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cwctype>
#include <memory>
#include <thread>

#include "../theme/theme.h"
#include "../overlay_fade.h"   // OverlayFade::now_ms()（cyberpunk 呼吸相位的时间基准）
#include "../soft_shadow.h"    // 外阴影/外发光近似（CSS box-shadow 的 D2D 替代）
#include "config/config.h"
#include "config/proxy_head_state.h"
#include "util/log.h"
#include "util/utf.h"
#include "ui/i18n_text.h"  // F-D01

namespace clashwin::ui::scene {

using theme::ThemeService;  // T12 主题服务（scene 层全量使用）

namespace utf = clashwin::utf;

namespace {
Color4f with_alpha_bg(const Color4f& c, float a) { return {c.r, c.g, c.b, a}; }

// 名称排序：src: use-filter-sort.ts:78 localeCompare（按系统默认 locale）
int locale_name_cmp(const std::string& a, const std::string& b) {
  std::wstring wa = utf::utf8_to_utf16(a), wb = utf::utf8_to_utf16(b);
  int r = ::CompareStringEx(LOCALE_NAME_USER_DEFAULT, 0, wa.c_str(),
                            static_cast<int>(wa.size()), wb.c_str(),
                            static_cast<int>(wb.size()), nullptr, nullptr, 0);
  return r == CSTR_LESS_THAN ? -1 : (r == CSTR_GREATER_THAN ? 1 : 0);
}

// 三态排序按钮：图标 ← proxy-head.tsx:178-198；Tooltip ← locales/zh/proxies.json:34-36。
// 提取为文件级常量，供恢复路径（restore_head_state）与切换路径（toggle_sort）复用，
// ← Mini proxy-head.tsx:188 sortType=(sortType+1)%3 三态循环 / use-head-state.ts:21 初值 1。
constexpr const char* kSortIcons[3] = {"sort", "history", "sortbyalpha"};
constexpr const char* kSortTipKeys[3] = {"proxies.page.tooltips.sortDefault",
                                          "proxies.page.tooltips.sortDelay",
                                          "proxies.page.tooltips.sortName"};
}  // namespace

// 延迟档位色：全部走主题 token（深浅自动正确），口径逐行对齐源码。
// src: services/delay.ts:408-412 formatDelayColor → MUI palette 变体 →
//      _theme.tsx:3-11(light)/16-26(dark) 的 .main 值；light Slow 用 warning.dark。
Color4f delay_tier_color(DelayTier tier) {
  const auto& svc = ThemeService::instance();
  const auto& pal = svc.palette();
  switch (tier) {
    case DelayTier::Success:
      return pal.success_color;  // src: success.main（#06943D/#30D158）
    case DelayTier::Mid:
      return pal.primary_color;  // src: primary.main（#007AFF/#0A84FF）
    case DelayTier::Slow:
      // src: delay.ts:410 light→warning.dark / dark→warning.main
      //   MUI 默认 warning.dark=orange[900] #E65100；_theme.tsx:9 仅覆盖 main
      return svc.is_light() ? Color4f::hex(0xE65100)  // warning.dark(light)
                            : pal.warning_color;       // warning.main(dark #FF9F0A)
    case DelayTier::Timeout:
      return pal.error_color;  // src: error.main（#FF3B30/#FF453A）
    default:
      return pal.secondary_text;  // src: delay.ts:418 text.secondary（未测灰）
  }
}

namespace {
// src: proxy-item.tsx:51「.replace(/\s\(\d{6}\)$/, '')」→ 名称清洗（含无括号、带括号、带空格变体）
std::wstring strip_six_digit_suffix(std::wstring text) {
  while (!text.empty() && (text.back() == L' ' || text.back() == L'\t')) {
    text.pop_back();
  }
  const size_t n = text.size();
  // 分支 1: " (123456)" 或 "(123456)"
  if (n >= 8 && text[n - 1] == L')' && text[n - 8] == L'(') {
    bool all_digits = true;
    for (size_t i = n - 7; i < n - 1; ++i) {
      if (text[i] < L'0' || text[i] > L'9') { all_digits = false; break; }
    }
    if (all_digits) {
      size_t cut = n - 8;
      if (cut > 0 && (text[cut - 1] == L' ' || text[cut - 1] == L'\t')) cut--;
      return text.substr(0, cut);
    }
  }
  // 分支 2: " 123456" 或 "-123456" 或 "_123456"
  if (n >= 7 && (text[n - 7] == L' ' || text[n - 7] == L'\t' || text[n - 7] == L'-' || text[n - 7] == L'_')) {
    bool all_digits = true;
    for (size_t i = n - 6; i < n; ++i) {
      if (text[i] < L'0' || text[i] > L'9') { all_digits = false; break; }
    }
    if (all_digits) {
      return text.substr(0, n - 7);
    }
  }
  return text;
}
}  // namespace

// src: src/components/proxy/use-proxy-delay-state.ts:61 PRESET_PROXY_NAMES 的延迟列不渲染
bool is_preset_proxy(const std::string& name) {
  return name == "DIRECT" || name == "REJECT" || name == "REJECT-DROP" ||
         name == "PASS" || name == "COMPATIBLE" || name == "GLOBAL";
}

std::string clean_node_display_name(const std::string& raw) {
  return utf::utf16_to_utf8(strip_six_digit_suffix(utf::utf8_to_utf16(raw)));
}

// ---------- NodeRowWidget ----------
bool NodeRowWidget::in_delay_zone(float local_x) const {
  return !is_preset_proxy(name_) && bounds().w > kNodeDelayColW &&
         local_x >= bounds().w - kNodeDelayColW;
}

void NodeRowWidget::bind(const std::string& type, const std::string& name,
                         int delay, bool selected) {
  if (type_ == type && name_ == name && delay_ == delay && selected_ == selected) {
    return;
  }
  type_ = type;
  name_ = name;
  delay_ = delay;
  selected_ = selected;
  invalidate();
}

void NodeRowWidget::on_paint(IRenderTarget& rt) {
  const auto& svc = ThemeService::instance();
  // 【SCROLLFIX】Widget::paint 已把绘制原点平移到本行全局位置（约定局部
  //   坐标 {0,0,w,h}）；此前直接用 bounds()（含 y=idx*rowH-offset），
  //   造成双重偏移 → 内容落在自身 39px 裁剪外 → 除表头外整列不可见。
  RectF r{0, 0, bounds().w, bounds().h};
  const auto& pal = svc.palette();

  // src: proxy-item.tsx:77-97 行底：奇数行 rgba(0,120,215,.04)/rgba(50,100,180,.08)；
  //   选中 alpha(primary.main,.15/.35) + borderLeft 3px（:84 selectColor=light
  //   primary.main / dark primary.light）；hover MUI 默认 .04/.08
  Color4f row_divider =
      svc.is_light() ? Color4f(0, 0, 0, 0.12f) : Color4f(1, 1, 1, 0.12f);  // :75,121,144 theme.palette.divider
  if (selected_) {
    Color4f sel_bg = pal.primary_color;
    sel_bg.a = svc.is_light() ? 0.15f : 0.35f;
    rt.fill_rect(r, Brush{sel_bg});
    // src: proxy-item.tsx:84,89 borderLeft 3px selectColor
    //   light: primary.main；dark: primary.light（MUI 派生 = lighten(main, 0.2)）
    Color4f sel_bar = pal.primary_color;
    if (!svc.is_light()) {
      sel_bar.r = sel_bar.r * 0.8f + 0.2f;
      sel_bar.g = sel_bar.g * 0.8f + 0.2f;
      sel_bar.b = sel_bar.b * 0.8f + 0.2f;
    }
    rt.fill_rect({r.x, r.y, 3, r.h}, Brush{sel_bar});
  } else if (index_in_group % 2 == 1) {
    rt.fill_rect(r, Brush{svc.is_light()
                              ? Color4f(0.f, 120 / 255.f, 215 / 255.f, 0.04f)
                              : Color4f(50 / 255.f, 100 / 255.f, 180 / 255.f,
                                        0.08f)});
  }
  if (hover_ && !selected_) {
    rt.fill_rect(r, Brush{svc.is_light() ? Color4f(0, 0, 0, 0.04f)
                                          : Color4f(1, 1, 1, 0.08f)});
  }

  Brush divider{row_divider};
  // src: proxy-render.tsx:89「pl: 2」→ 行内容左内距 16px
  float x = r.x + 16.0f;
  float text_h = r.h;

  // 左列：类型（11px 灰大写右对齐、右边框 2px divider）
  RectF type_r{x, r.y, kNodeTypeColW, r.h};
  TextStyle ts;
  ts.size = 11;
  ts.color = pal.secondary_text;  // src: proxy-item.tsx:118 color text.secondary
  ts.align_v = AlignV::Center;
  ts.align_h = AlignH::Right;
  ts.ellipsis = true;
  // src: proxy-item.tsx:120「textTransform: 'uppercase'」
  std::wstring wtype = utf::utf8_to_utf16(type_);
  for (auto& c : wtype)
    c = static_cast<wchar_t>(std::towupper(static_cast<wint_t>(c)));
  rt.draw_text(wtype, {type_r.x, type_r.y, type_r.w - 4, text_h}, ts);
  rt.fill_rect({type_r.right() - kNodeBorderW, r.y, kNodeBorderW, r.h},
               divider);
  x = type_r.right();

  // 名称区（12px text.primary；selected bold；减 16.0f 边距居中延迟列）
  // src: proxy-item.tsx:142；窄卡片（3 列模式）下宽度下限保护，避免负宽
  float name_w = r.w - 16.0f - kNodeTypeColW - kNodeDelayColW;
  if (name_w < 0) name_w = 0;
  RectF name_r{x, r.y, name_w, r.h};
  TextStyle ns;
  ns.size = 12;
  ns.weight = selected_ ? static_cast<int>(Weight::Bold)
                        : static_cast<int>(Weight::Normal);
  // src: proxy-item.tsx:142 color text.primary
  ns.color = pal.primary_text;
  ns.align_v = AlignV::Center;
  ns.ellipsis = true;
  // src: proxy-item.tsx:51 名称清洗；:185-193 selected 时追加「 - 当前节点」
  //   （text.secondary / 11px / ml 0.5=4）
  float suffix_w = 0;
  if (selected_ && !name_.empty()) {
    const std::wstring suffix = L" - 当前节点";
    TextStyle ss;
    ss.size = 11;
    ss.color = pal.secondary_text;
    ss.align_v = AlignV::Center;
    ss.align_h = AlignH::Right;
    suffix_w = text_engine()->measure_text(suffix, ss, 1e6f).w + 4.0f;
    rt.draw_text(suffix, {name_r.right() - 8 - suffix_w, name_r.y, suffix_w, text_h},
                 ss);
  }
  rt.draw_text(strip_six_digit_suffix(utf::utf8_to_utf16(name_)),
               {name_r.x + 8, name_r.y, name_r.w - 16 - suffix_w, text_h}, ns);
  rt.fill_rect({name_r.right() - kNodeBorderW, r.y, kNodeBorderW, r.h},
               divider);

  // 右延迟列 65px（11px bold600 居中、五档色）
  {
    RectF delay_r{name_r.right(), r.y, kNodeDelayColW, r.h};
    // src: proxy-item.tsx:203「display: isPreset ? 'none' : 'flex'」
    if (is_preset_proxy(name_)) return;
    if (delay_ == -2) {
      // src: proxy-item.tsx:209-212 delayValue===-2 渲染 BaseLoading（三点脉冲，
      //   非静态文字）；src: base-loading.tsx:13-33 6px 圆点 / margin 2px /
      //   animation 0.7s linear infinite / nth-child(2n-1) delay -0.5s（中间 -0.15s）/
      //   50% opacity .2 + scale .75、100% opacity 1。
      constexpr float dot = 6.0f, margin = 2.0f;
      const float total = 3 * dot + 6 * margin;  // 每点 6px + 左右各 2px
      const float cy = delay_r.y + delay_r.h / 2;
      constexpr double cycle_ms = 700.0;
      const double t = static_cast<double>(GetTickCount64());
      Color4f base = pal.secondary_text;  // src: base-loading.tsx:36-38 text.secondary
      for (int i = 0; i < 3; ++i) {
        const double off = (i == 1) ? 0.15 / 0.7 : 0.5 / 0.7;
        const double p = std::fmod(t / cycle_ms + off, 1.0);
        const float k = static_cast<float>(p < 0.5 ? p / 0.5 : (p - 0.5) / 0.5);
        const float alpha = p < 0.5 ? 1.0f - 0.8f * k : 0.2f + 0.8f * k;
        const float scale = p < 0.5 ? 1.0f - 0.25f * k : 0.75f + 0.25f * k;
        const float d = dot * scale;
        const float cx = delay_r.x + (delay_r.w - total) / 2 + margin +
                         i * (dot + 2 * margin) + dot / 2;
        Color4f c = base;
        c.a = alpha;
        Brush dot_brush{c};
        rt.round_rect({cx - d / 2, cy - d / 2, d, d}, d / 2, Pen{base, 0.0f},
                      &dot_brush);
      }
      // 动画期间自续帧（30fps 帧泵在下一 tick 重绘本行脏区）
      invalidate();
    } else {
      // src: src/components/proxy/proxy-item.tsx:255-280 延迟列文本与悬停下划线
      TextStyle ds;
      ds.size = 11;    // src: src/components/proxy/proxy-item.tsx:260
      ds.weight = 600; // src: src/components/proxy/proxy-item.tsx:274
      ds.color = delay_tier_color(delay_tier(delay_));
      ds.align_h = AlignH::Center;
      ds.align_v = AlignV::Center;
      const std::wstring dtxt = format_delay(delay_);
      rt.draw_text(dtxt,
                   {delay_r.x + 3, delay_r.y, delay_r.w - 6, r.h - kNodeBorderW}, ds);  // src: src/components/proxy/proxy-item.tsx:256
      if (hover_ && in_delay_zone(hover_x_)) {
        float tw = 12.0f;  // src: src/components/proxy/proxy-item.tsx:244
        if (ITextEngine* eng = text_engine()) {
          tw = std::min(delay_r.w - 6.0f, eng->measure_text(dtxt, ds, 0).w);  // src: src/components/proxy/proxy-item.tsx:244
        }
        const float ux = delay_r.x + (delay_r.w - tw) * 0.5f;
        const float uy = delay_r.y + (r.h - kNodeBorderW) * 0.5f + 6.5f;  // src: src/components/proxy/proxy-item.tsx:244
        rt.fill_rect({ux, uy, std::max(0.0f, tw), 1.0f}, Brush{ds.color});  // src: src/components/proxy/proxy-item.tsx:244
      }
    }
  }

  // 行底边框 2px divider
  rt.fill_rect({r.x, r.bottom() - kNodeBorderW, r.w, kNodeBorderW}, divider);
}

HCURSOR NodeRowWidget::cursor(const PointF& local) const {
  if (in_delay_zone(local.x)) {
    return LoadCursorW(nullptr, delay_ == -2 ? IDC_WAIT : IDC_HAND);  // src: src/components/proxy/proxy-item.tsx:242
  }
  return LoadCursorW(nullptr, IDC_HAND);
}

bool NodeRowWidget::on_mouse(const MouseEvent& e) {
  if (e.type == MouseEventType::Move || e.type == MouseEventType::Enter) {
    const bool prev_hover = hover_;
    const bool prev_in_delay = hover_ && in_delay_zone(hover_x_);
    hover_ = true;
    hover_x_ = e.pos.x;
    if (!prev_hover || prev_in_delay != in_delay_zone(hover_x_)) {
      invalidate();
    }
    return true;
  }
  if (e.type == MouseEventType::Leave) {
    hover_ = false;
    down_in_delay_zone_ = false;
    invalidate();
    return true;
  }
  // 【CLICKFIX】Down 必须返回 true 拿到 capture，否则 Up 会被祖先控件截走
  if (e.type == MouseEventType::Down) {
    down_in_delay_zone_ = in_delay_zone(e.pos.x);
    return true;
  }
  if (e.type == MouseEventType::Up) {
    const bool up_in_delay = in_delay_zone(e.pos.x);
    const bool was_down_in_delay = down_in_delay_zone_;
    down_in_delay_zone_ = false;
    // BUG-357: 右侧 65px 延迟热区点击触发单节点测速（stopPropagation 不切节点）
    if (was_down_in_delay || up_in_delay) {
      if (was_down_in_delay && up_in_delay && delay_ != -2 && on_test_single) {  // src: src/components/proxy/proxy-item.tsx:233
        on_test_single();
      }
      return true;
    }
    if (on_click) on_click();
    return true;
  }
  return false;
}

std::wstring NodeRowWidget::tooltip_text() const {
  if (!hover_) return {};
  // src: src/components/proxy/proxy-item.tsx:160 左侧协议列悬停显示协议类型
  if (hover_x_ < kNodeTypeColW) {
    return clashwin::utf::utf8_to_utf16(type_);
  }
  // src: src/components/proxy/proxy-item.tsx:180 名称列悬停显示完整名称（选中项附「 - 当前节点」）
  if (hover_x_ < bounds().w - kNodeDelayColW) {
    std::wstring tip = clashwin::utf::utf8_to_utf16(name_);
    if (selected_) tip += L" - " + tr("proxies.components.item.currentNode");
    return tip;
  }
  // BUG-357: 右侧 65px 延迟列悬停提示单节点测速（预设节点除外）
  if (!is_preset_proxy(name_)) {
    return tr("settings.mini.testSingleNodeTooltip");
  }
  return {};
}

std::optional<RectF> NodeRowWidget::tooltip_anchor_rect() const {
  if (!hover_ || bounds().w <= 0.0f) return std::nullopt;
  const RectF g = global_bounds();
  if (hover_x_ < kNodeTypeColW) {
    return RectF{g.x, g.y, std::min(kNodeTypeColW, g.w), g.h};
  }
  if (hover_x_ < bounds().w - kNodeDelayColW) {
    const float nx = std::min(kNodeTypeColW, g.w);
    const float nw = std::max(0.0f, g.w - kNodeDelayColW - nx);
    return RectF{g.x + nx, g.y, nw, g.h};
  }
  if (!is_preset_proxy(name_)) {
    return RectF{g.x + g.w - kNodeDelayColW, g.y, kNodeDelayColW, g.h};
  }
  return std::nullopt;
}

std::wstring NodeRowWidget::a11y_default_name() const {
  std::wstring out = strip_six_digit_suffix(utf::utf8_to_utf16(name_));
  if (!type_.empty()) {
    std::wstring wtype = utf::utf8_to_utf16(type_);
    for (auto& c : wtype)
      c = static_cast<wchar_t>(std::towupper(static_cast<wint_t>(c)));
    if (!out.empty()) out += L" ";
    out += L"(" + wtype + L")";
  }
  if (!is_preset_proxy(name_)) {
    std::wstring d = (delay_ == -2) ? L"..." : format_delay(delay_);
    if (!d.empty()) {
      if (delay_ > 0 && delay_ < kNodeDelayMaxMs) d += L"ms";
      if (!out.empty()) out += L" ";
      out += d;
    }
  }
  if (selected_) {
    out += L" - " + tr("shared.statuses.enabled");
  }
  return out;
}

// ---------- NodeGridRowWidget ----------
void NodeGridRowWidget::set_cards(const std::vector<GridCardItem>& items, int cols) {
  cols_ = cols;
  for (const auto& it : items) {
    auto card = std::make_unique<NodeRowWidget>();
    card->bind(it.type, it.name, it.delay, it.selected);
    card->index_in_group = it.index;
    card->pos_in_set = it.pos_in_set;
    card->set_size = it.set_size;
    card->on_click = [this, name = it.name]() {
      if (on_select) on_select(name);
    };
    card->on_test_single = [this, name = it.name]() {
      if (on_test_single) on_test_single(name);
    };
    NodeRowWidget* raw = card.get();
    add_child(std::move(card));
    cards_.push_back(raw);
  }
  invalidate();
}

void NodeGridRowWidget::update_cards_in_place(const std::vector<GridCardItem>& items) {
  const size_t n = std::min(cards_.size(), items.size());
  for (size_t i = 0; i < n; ++i) {
    NodeRowWidget* card = cards_[i];
    if (!card) continue;
    const auto& it = items[i];
    card->index_in_group = it.index;
    card->pos_in_set = it.pos_in_set;
    card->set_size = it.set_size;
    card->on_click = [this, name = it.name]() {
      if (on_select) on_select(name);
    };
    card->on_test_single = [this, name = it.name]() {
      if (on_test_single) on_test_single(name);
    };
    card->bind(it.type, it.name, it.delay, it.selected);
  }
}

void NodeGridRowWidget::on_arrange(const RectF& area) {
  set_bounds(area);
  const float card_w = (cols_ > 0) ? area.w / static_cast<float>(cols_) : area.w;
  for (size_t i = 0; i < cards_.size(); ++i) {
    cards_[i]->layout_tree(
        {static_cast<float>(i) * card_w, 0.0f, card_w, area.h});
  }
}

void NodeGridRowWidget::on_paint(IRenderTarget& rt) {
  // 列间 5px double 竖边框（← proxy-render.tsx:44 borderRight:'5px double
  //   var(--theme-border)'）；on_paint 先于子控件绘制，卡片透明区域透出分隔线。
  const auto& svc = ThemeService::instance();
  const auto pp = svc.panel_paint();
  Color4f bc = pp.border_color;
  if (pp.pulse) {
    const float now_s = static_cast<float>(GetTickCount64()) / 1000.0f;
    bc = Color4f::lerp(pp.pulse_from, pp.pulse_to, svc.pulse_phase(now_s));
  }
  if (cols_ <= 1) return;
  const float w = bounds().w, h = bounds().h;
  const float card_w = w / static_cast<float>(cols_);
  for (int i = 0; i + 1 < cols_; ++i) {
    const float x = (i + 1) * card_w;  // 简化为双线夹 1px 空隙近似 double
    rt.fill_rect({x - 2.5f, 1.0f, 1.34f, h - 2.0f}, Brush{bc});
    rt.fill_rect({x + 1.5f, 1.0f, 1.34f, h - 2.0f}, Brush{bc});
  }
}

// ---------- ScrollTopButton ----------
// ← src/components/layout/scroll-top-button.tsx:10-40 + proxy-groups.tsx:171-176,212-220
ScrollTopButton::ScrollTopButton(Widget* parent) : Widget(parent) {
  auto ic = std::make_unique<IconW>();
  icon_ = ic.get();
  icon_->set_icon("keyboardarrowup");  // ← KeyboardArrowUpIcon
  icon_->size_ = 24;                   // MUI SvgIcon 默认 fontSize medium = 1.5rem
  icon_->set_a11y_hidden(true);
  add_child(std::move(ic));
}

void ScrollTopButton::set_shown(bool show) {
  if (shown_ == show) return;
  shown_ = show;
  // <Fade in={show}>：MUI 默认进入 225ms / 退出 195ms（theme.transitions.duration）
  anim_from_ = alpha_;
  anim_to_ = show ? 1.0f : 0.0f;
  anim_start_ms_ = static_cast<int64_t>(::GetTickCount64());
  anim_dur_ms_ = show ? 225 : 195;
  invalidate();
}

void ScrollTopButton::advance_anim(int64_t now_ms) {
  if (anim_from_ == anim_to_) return;
  const float p = anim_dur_ms_ <= 0
                      ? 1.0f
                      : std::min(1.0f, static_cast<float>(now_ms - anim_start_ms_) /
                                           static_cast<float>(anim_dur_ms_));
  alpha_ = anim_from_ + (anim_to_ - anim_from_) * p;
  if (p >= 1.0f) alpha_ = anim_to_;
}

SizeF ScrollTopButton::on_measure(float, float) {
  return {kScrollTopBtnSize, kScrollTopBtnSize};
}

void ScrollTopButton::on_arrange(const RectF& area) {
  set_bounds(area);
  // 图标居中：padding 8 使 24px 图标居中于 40×40 圆
  const float pad = (kScrollTopBtnSize - 24.0f) / 2.0f;
  icon_->layout_tree({pad, pad, 24, 24});
}

void ScrollTopButton::on_paint(IRenderTarget& rt) {
  advance_anim(static_cast<int64_t>(::GetTickCount64()));
  if (alpha_ > 0.0f) invalidate();  // 自续帧（同 NodeRowWidget 圆点动画路径）
  // 子控件由 Widget::paint 自动绘制（带自身原点半移），父层只调色与显隐
  if (icon_) {
    icon_->set_visible(alpha_ > 0.0f);
    Color4f ic = ThemeService::instance().text_primary();
    ic.a = alpha_;  // <Fade> 统一作用于整颗按钮
    icon_->color_ = ic;
  }
  if (alpha_ <= 0.0f) return;

  const auto& svc = ThemeService::instance();
  // src: components/layout/scroll-top-button.tsx:19-27
  //   backgroundColor: dark ? rgba(255,255,255,.1) : rgba(0,0,0,.1)
  //   '&:hover' → dark ? rgba(255,255,255,.2) : rgba(0,0,0,.2)
  const bool light = svc.is_light();
  const Color4f rgb = light ? Color4f(0, 0, 0, 1) : Color4f(1, 1, 1, 1);
  const float bg_a = (hover_ ? 0.2f : 0.1f) * alpha_;
  Brush bg{with_alpha_bg(rgb, bg_a)};
  const RectF r{0, 0, kScrollTopBtnSize, kScrollTopBtnSize};
  // 豁免: Mini 无对应物 — D2D 无边框填充须传透明 Pen（出处: 无）
  rt.round_rect(r, kScrollTopBtnSize / 2.0f, Pen{Color4f(0, 0, 0, 0), 0.0f}, &bg);
}

bool ScrollTopButton::on_mouse(const MouseEvent& e) {
  if (!interactive()) return false;  // 淡出完成后不可点
  if (e.type == MouseEventType::Move) {
    if (!hover_) {
      hover_ = true;
      invalidate();
    }
    return true;
  }
  if (e.type == MouseEventType::Leave) {
    if (hover_) {
      hover_ = false;
      invalidate();
    }
    return true;
  }
  if (e.type == MouseEventType::Down) return true;  // 拿 capture，否则 Up 被祖先截走
  if (e.type == MouseEventType::Up) {
    if (on_click) on_click();
    return true;
  }
  return false;
}

Widget* ScrollTopButton::hit_test(PointF global) {
  if (!interactive()) return nullptr;  // visibility:hidden → 穿透回列表
  return Widget::hit_test(global);
}

// ---------- NodeList ----------
NodeList::NodeList(Widget* parent) : Widget(parent) {
  const auto& pal = ThemeService::instance().palette();

  // 工具条：src: proxy-head.tsx:70-79（flex / gap 1=8 / borderBottom 1px divider
  //   / pb 0.5=4 / mb 0.5=4）+ proxy-groups.tsx:395-397（px 2=16 / py 0.5=4）
  auto bar = std::make_unique<Row>();
  Row* bar_p = bar.get();
  bar_p->set_padding(16, 4, 16, 4);
  bar_p->spacing = 8;
  bar_p->cross_align = CrossAlign::Center;
  add_child(std::move(bar));

  // ① ⚡ 延迟测试：IconButton 26×26 / BoltOutlined 17 / color inherit
  // src: proxy-head.tsx:81-107（26×26 :90，fontSize 17 :95）
  auto bolt = std::make_unique<IconW>();
  bolt->set_icon("bolt");
  bolt->size_ = 17;
  bolt->box_w = bolt->box_h = 26;
  bolt->color_ = pal.primary_text;
  bolt->flex_weight = 0;
  // src: proxy-head.tsx:145 title={t('proxies.page.tooltips.delayCheck')}
  bolt->set_i18n_tooltip("proxies.page.tooltips.delayCheck");
  bolt->set_i18n_a11y_name("proxies.page.tooltips.delayCheck");
  bolt_btn_ = bolt.get();
  bolt->on_click = [this] {
    // ★ §5.2/§5.6「所见即所选」（← Mini proxy-groups.tsx:230-275 handleCheckAll）：
    //   测速范围 = 当前过滤后**可见**的节点子集，择优切换也限该子集。
    //   与「全量自动优选」（F1 语义，走 AutoSelect 不传子集）区分。
    if (!data_) return;
    std::vector<std::string> names;
    const auto& rows = data_->rows();
    names.reserve(visible_.size());
    for (int i : visible_) {
      if (i < 0 || static_cast<size_t>(i) >= rows.size()) continue;
      names.push_back(rows[static_cast<size_t>(i)].name);
    }
    if (names.empty()) return;
    // BUG-321: 置 -2 前先拍摄延迟快照，异常/繁忙/空结果结束时立即回滚仍处于 -2 的节点
    const auto snaps = data_->snapshot_delays(names);
    data_->set_delays_batch(names, -2);  // src: src/services/delay.ts:456
    set_testing(true);
    invalidate();
    const uint64_t wait_id = on_batch_test_begin ? on_batch_test_begin() : 0;
    auto end_cb = on_batch_test_end;
    auto* data_ptr = data_;
    auto alive = alive_;
    std::thread([this, alive, data_ptr, names, snaps, wait_id, end_cb] {
      data_ptr->test_and_select(names);
      const BatchTestSummary sum = data_ptr->last_batch_summary();
      UiPostbox::instance().post([this, alive, data_ptr, snaps, end_cb, wait_id, sum] {
        if (alive && alive->load()) {
          data_ptr->restore_delays_for_still_pending(snaps);
          set_testing(false);
        }
        if (end_cb) end_cb(wait_id, sum);
      });
    }).detach();
  };
  bar_p->add_child(std::move(bolt));

  // ② 排序：IconButton 26×26 / 图标 17 / 3 态循环
  // src: proxy-head.tsx:109-140（26×26 :129，fontSize 17 :131-139）
  auto sort = std::make_unique<IconW>();
  sort_btn_ = sort.get();
  // src: use-head-state.ts:21 sortType 初值 1 ⇒ AccessTimeOutlined + 按延迟排序
  sort->set_icon("history");
  sort->size_ = 17;
  sort->box_w = sort->box_h = 26;
  sort->color_ = pal.primary_text;
  sort->set_i18n_tooltip("proxies.page.tooltips.sortDelay");  // src: locales/zh/proxies.json:34-36
  sort->set_i18n_a11y_name("proxies.page.tooltips.sortDelay");
  sort->on_click = [this] { toggle_sort(); };
  bar_p->add_child(std::move(sort));

  // ③ 搜索框外层：ml 0.5=4 / flex 1 1 auto
  // src: proxy-head.tsx:142-148
  auto search_wrap = std::make_unique<Box>();
  search_wrap_ = search_wrap.get();
  search_wrap_->set_padding(4, 0, 0, 0);
  search_wrap_->flex_weight = 1;
  search_wrap_->shrink_to_free = true;  // TextField measure 返回 max_w，需允许收缩
  bar_p->add_child(std::move(search_wrap));

  // 搜索框：input py .3 / px .5 / fontSize 11（src: proxy-head.tsx:146）；
  //   get3DInputStyle 2px 金描边 + dark 底 rgba(26,22,14,.4)
  // src: button-styles.ts:734-778；base-search-box.tsx:22-34
  auto search = std::make_unique<TextField>();
  search_ = search.get();
  search_->flex_weight = 1;
  // placeholder src: base-search-box.tsx:117 shared.placeholders.filter
  search_->set_placeholder(tr("shared.placeholders.filter"));
  search_->set_i18n_a11y_name("shared.placeholders.filter");
  // src: button-styles.ts:734-927 get3DInputStyle（6 套皮肤全部走 apply_theme）
  search_->apply_theme();
  search_->style_.size = 11;
  search_->caret_color_ = search_->style_.color;
  // search_->radius_ 由 apply_theme() 按皮肤设置（4*g / 6*g / 8*g）
  search_->height_ = 26;
  // 清空按钮（opt-in）：src: base-search-box.tsx:121-140
  //   ClearRounded 18 / pr 4 / 空值 opacity .5 + disabled / Tooltip resetInput
  search_->set_clear_button(true, tr("shared.placeholders.resetInput"));  // src: src/components/base/base-search-box.tsx:126
  // BUG-360: Escape 键在自动测速倒计时挂起期间优先取消倒计时，保留过滤词与列表
  search_->on_key_intercept = [this](const KeyEvent& ke) {
    if (ke.type == KeyEventType::Down && ke.key == VK_ESCAPE) {
      if (cancel_pending_filter_auto_test()) return true;
    }
    return false;
  };
  search_->on_text_changed = [this](const std::wstring& t) {
    set_filter(clashwin::utf::utf16_to_utf8(t));
    ++filter_change_seq_;
    if (filter_.empty() || visible_.empty() || filter_ == last_auto_select_filter_) {
      cancel_pending_filter_auto_test();
      return;
    }
    // BUG-360: 使用可取消的 700ms 宿主定时器替代后台游离 sleep 线程
    pending_auto_test_ = true;
    refresh_filter_spinner();
    if (on_arm_filter_auto_test_timer) {
      on_arm_filter_auto_test_timer(700);  // src: src/components/proxy/proxy-head.tsx:148
    }
  };
  search_->on_submit = [this] {
    cancel_pending_filter_auto_test();
    // G4: 同过滤词不重复触发（← Mini proxy-head.tsx lastTriggeredFilterRef）
    if (!filter_.empty() && !visible_.empty() && filter_ != last_auto_select_filter_) {
      last_auto_select_filter_ = filter_;
      trigger_auto_select_visible();
    }
  };
  // G2: 删除 Win 加戏的失焦触发通道——Mini 只 onChangeText / onEnter 两通道
  //     （proxy-head.tsx:62-122 无 onBlur 触发），对齐铁律照删。
  search_wrap_->add_child(std::move(search));

  // 清空按钮由 TextField::set_clear_button 内建承载（配置见上）
  // src: base-search-box.tsx:121-140（ClearRounded 18 / pr 4 / opacity .5 / disabled）

  // 虚拟滚动列表（行高 26：src: proxy-item.tsx:67 minHeight 24 + :75 底边 2）
  auto list = std::make_unique<ScrollList>();
  list_ = list.get();
  list_->flex_weight = 1;
  list_->themed_ = true;
  // src: proxy-groups.tsx:413 `scrollbarWidth: 'none'`（:416 同为 none，列表无滚动条）
  list_->set_show_scrollbar(false);
  list_->set_a11y_role(A11yRole::List);
  list_->set_i18n_a11y_name("proxies.page.a11y.nodelist");  // src: src/components/proxy/proxy-groups.tsx:412
  add_child(std::move(list));

  // 空态：InboxRounded 2.5em(=40px) + "No Proxies"，居中
  // src: proxy-render.tsx:63-78（py 16 / 图标 fontSize 2.5em / 文字 color inherit）
  auto ei = std::make_unique<IconW>();
  empty_icon_ = ei.get();
  ei->set_icon("inbox");
  ei->size_ = 40;  // 2.5em × 16px
  ei->box_w = ei->box_h = 40;
  ei->color_ = pal.primary_text;  // color 'inherit' → text.primary
  ei->set_visible(false);
  ei->set_a11y_hidden(true);
  add_child(std::move(ei));

  auto el = std::make_unique<Label>();
  empty_label_ = el.get();
  el->style.size = 16;  // MUI Typography 默认 body1 1rem=16px
  el->style.color = pal.primary_text;
  el->set_text(L"No Proxies");  // src: proxy-render.tsx:76
  el->set_visible(false);
  add_child(std::move(el));

  // 滚动置顶按钮：src: proxy-groups.tsx:330 <ScrollTopButton show={showScrollTop}
  //   onClick={scrollToTop}/>，显隐由列表滚动位置驱动（:172 scrollTop > 100）
  auto stb = std::make_unique<ScrollTopButton>();
  scroll_top_ = stb.get();
  scroll_top_->set_shown(false);
  scroll_top_->on_click = [this] {
    if (list_) list_->set_scroll_offset(0);  // ← :212-220 scrollTo({top:0})
    scroll_top_->set_shown(false);
  };
  add_child(std::move(stb));

  if (list_) {
    list_->on_scroll = [this] {
      if (!list_ || !scroll_top_) return;
      scroll_top_->set_shown(list_->scroll_offset() > kScrollTopShowPx);
    };
  }
}

NodeList::~NodeList() {
  if (alive_) alive_->store(false);
  if (on_cancel_filter_auto_test_timer) on_cancel_filter_auto_test_timer();
}

void NodeList::focus_filter_input() {
  if (search_) search_->select_all();
}

void NodeList::refresh_filter_spinner() {
  if (!search_) return;
  const bool show = pending_auto_test_ || is_testing_;
  search_->set_end_spinner(
      show, show ? tr("settings.mini.filterAutoTestingTooltip") : std::wstring{});
}

void NodeList::set_testing(bool testing) {
  if (is_testing_ == testing) return;
  is_testing_ = testing;
  refresh_filter_spinner();
}

bool NodeList::cancel_pending_filter_auto_test() {
  if (!pending_auto_test_) return false;
  ++filter_change_seq_;
  pending_auto_test_ = false;
  if (on_cancel_filter_auto_test_timer) on_cancel_filter_auto_test_timer();
  refresh_filter_spinner();
  return true;
}

void NodeList::fire_filter_auto_test_timer() {
  if (!pending_auto_test_) return;
  pending_auto_test_ = false;
  refresh_filter_spinner();
  if (!filter_.empty() && !visible_.empty() && filter_ != last_auto_select_filter_) {
    last_auto_select_filter_ = filter_;
    trigger_auto_select_visible();
  }
}

void NodeList::bind(NodeData* data) {
  data_ = data;
  if (!data_) return;
  data_->subscribe([this]() {
    rebuild_visible();
    sync_list();
  });
  // ← Mini use-head-state.ts:68-99：组件挂载即读回整份 head state；
  //   须在重建列表前完成，否则首屏会用空过滤词/默认排序渲染。
  restore_head_state();
  rebuild_visible();
  sync_list();
}

void NodeList::restore_head_state() {
  // 一次性守卫：防重复 bind / profile 切换覆盖用户后续输入
  // ← Mini use-head-state.ts:66-99 isLoadedRef（仅挂载加载一次）。
  if (head_state_loaded_) return;

  // 当前 profile uid（取法同 set_filter/toggle_sort；← Mini use-head-state.ts:64
  //   profiles.current）。uid 为空（profile 尚未激活）不置 loaded，允许激活后
  //   经 rebuild_visible() 的懒加载兜底再次尝试。
  std::string uid;
  auto latest = config::Config::instance().profiles().latest_arc();
  if (latest && latest->current) uid = *latest->current;
  if (uid.empty()) return;

  // ★ 先置守卫再读档/回填：本函数末尾会调 rebuild_visible()，而 rebuild_visible()
  //   开头有「!head_state_loaded_ → restore_head_state()」的懒加载兜底，
  //   先置 true 可断开递归；uid 为空时上面的 return 不回调 rebuild，天然无环。
  head_state_loaded_ = true;

  auto st = config::proxy_head_state::load();
  auto gs = config::proxy_head_state::get_group_state(st, uid, "PROXY");
  // 无记录：保持默认（filter 空、sortType 1，← use-head-state.ts:21），
  //   由调用方（bind / rebuild_visible 懒加载）继续重建列表。
  if (!gs) return;

  // filter_：与 set_filter() 完全相同的 trim + tolower 口径
  // （← Mini use-filter-sort.ts:29 query.trim；大小写不敏感子串匹配）。
  std::string trimmed = gs->filter_text;
  trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
  trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
  std::string lower;
  lower.reserve(trimmed.size());
  for (char c : trimmed)
    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  filter_ = lower;

  // ← Mini proxy-head.tsx:188 sortType 三态（0/1/2）；落盘值 clamp 防越界。
  sort_type_ = static_cast<int>(std::clamp<int64_t>(gs->sort_type, 0, 2));

  // 受控回填：用原始大小写显示，且绝不触发 on_text_changed（不启动 700ms 去抖测速）
  // ← Mini proxy-head.tsx:212 value={filterText} + base-search-box.tsx:95-97
  //   useEffect(emitSearch)→onSearch 通道，不经 proxy-head.tsx:213 onChangeText。
  if (search_) search_->set_text_silent(utf::utf8_to_utf16(gs->filter_text));
  if (sort_btn_) {
    sort_btn_->set_icon(kSortIcons[sort_type_]);
    sort_btn_->set_i18n_tooltip(kSortTipKeys[sort_type_]);
    sort_btn_->set_i18n_a11y_name(kSortTipKeys[sort_type_]);
  }

  // 兜底：恢复出的过滤词不视为「新词」，避免后续被误判触发自动测速择优
  // ← Mini proxy-head.tsx:63 lastTriggeredFilterRef 初值 = filterText。
  last_auto_select_filter_ = filter_;

  // 恢复路径只动 UI 内存态，绝不调 set_text()（启动去抖线程）/ set_filter()（重复落盘）
  // ← Mini use-head-state.ts:101-110：初始加载态跳过保存。
  rebuild_visible();
  sync_list();
}

void NodeList::set_filter(const std::string& text) {
  // src: use-filter-sort.ts:29「const query = (filterText || '').trim()」
  std::string trimmed = text;
  trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
  trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
  std::string lower;
  lower.reserve(trimmed.size());
  for (char c : trimmed)
    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (filter_ == lower) return;
  filter_ = lower;
  if (search_) {
    std::string cur_s = utf::utf16_to_utf8(search_->text());
    cur_s.erase(0, cur_s.find_first_not_of(" \t\r\n"));
    cur_s.erase(cur_s.find_last_not_of(" \t\r\n") + 1);
    std::string cur_lower;
    cur_lower.reserve(cur_s.size());
    for (char c : cur_s)
      cur_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (cur_lower != lower) {
      search_->set_text_silent(utf::utf8_to_utf16(trimmed));
    }
  }
  // G4: 过滤词清空时重置去重记录，保证清空后重输同一词仍可触发
  if (filter_.empty()) last_auto_select_filter_.clear();
  try {
    std::string uid;
    auto latest = config::Config::instance().profiles().latest_arc();
    if (latest && latest->current) uid = *latest->current;
    if (!uid.empty()) {
      auto st = config::proxy_head_state::load();
      config::proxy_head_state::GroupHeadState gs;
      auto old_gs = config::proxy_head_state::get_group_state(st, uid, "PROXY");
      if (old_gs) gs = *old_gs;
      // 落盘用户输入原文（trim 后），不落小写态 ← Mini use-head-state.ts:101-110
      //   保存的 filterText 是受控原文（proxy-head.tsx:212 value={filterText}）；
      //   小写态只用于内存匹配（filter_）。消费方 core::extract_filter_text
      //   （core_manager.cpp:352-362）与 calibrate_selected 调用点自带 tolower。
      gs.filter_text = trimmed;
      gs.sort_type = sort_type_;
      auto updated = config::proxy_head_state::set_group_state(st, uid, "PROXY", gs);
      config::proxy_head_state::save(updated);
    }
  } catch (...) {}
  rebuild_visible();
  sync_list();
}

void NodeList::toggle_sort() {
  // src: proxy-head.tsx:126-128 sortType=(sortType+1)%3
  sort_type_ = (sort_type_ + 1) % 3;
  try {
    std::string uid;
    auto latest = config::Config::instance().profiles().latest_arc();
    if (latest && latest->current) uid = *latest->current;
    if (!uid.empty()) {
      auto st = config::proxy_head_state::load();
      config::proxy_head_state::GroupHeadState gs;
      auto old_gs = config::proxy_head_state::get_group_state(st, uid, "PROXY");
      if (old_gs) gs = *old_gs;
      // toggle_sort 只改排序：filter_text 保留磁盘原文不覆盖（← Mini
      //   use-head-state.ts:101-110 按 key 局部 patch，sortType 变更不重写 filterText）。
      gs.sort_type = sort_type_;
      auto updated = config::proxy_head_state::set_group_state(st, uid, "PROXY", gs);
      config::proxy_head_state::save(updated);
    }
  } catch (...) {}
  // 图标 3 态：SortOutlined / AccessTimeOutlined / SortByAlphaOutlined
  // src: proxy-head.tsx:131-139
  if (sort_btn_) {
    sort_btn_->set_icon(kSortIcons[sort_type_]);
    sort_btn_->set_i18n_tooltip(kSortTipKeys[sort_type_]);
    sort_btn_->set_i18n_a11y_name(kSortTipKeys[sort_type_]);
  }
  rebuild_visible();
  sync_list();
  if (on_sort_changed) on_sort_changed(sort_type_);
}

// 皮肤/亮暗热切换：重刷控件级缓存
// src: button-styles.ts:734-927 get3DInputStyle —— 每次 render 现取，
//   Win 侧 apply_theme 把结果缓存进成员，切皮肤后必须重调。
std::vector<std::string> NodeList::visible_node_names() const {
  std::vector<std::string> out;
  if (!data_) return out;
  const auto& rows = data_->rows();
  out.reserve(visible_.size());
  for (int i : visible_) {
    if (i >= 0 && static_cast<size_t>(i) < rows.size()) {
      out.push_back(rows[static_cast<size_t>(i)].name);
    }
  }
  return out;
}

void NodeList::trigger_auto_select_visible() {
  if (!data_ || visible_.empty()) return;
  std::vector<std::string> names;
  const auto& rows = data_->rows();
  names.reserve(visible_.size());
  for (int i : visible_) {
    if (i >= 0 && static_cast<size_t>(i) < rows.size()) {
      names.push_back(rows[static_cast<size_t>(i)].name);
    }
  }
  if (names.empty()) return;

  // BUG-321: 置 -2 前先拍摄延迟快照，异常/繁忙/空结果结束时立即回滚仍处于 -2 的节点
  const auto snaps = data_->snapshot_delays(names);
  data_->set_delays_batch(names, -2);  // src: src/services/delay.ts:456
  set_testing(true);
  invalidate();

  const uint64_t wait_id = on_batch_test_begin ? on_batch_test_begin() : 0;
  auto end_cb = on_batch_test_end;

  auto* data_ptr = data_;
  auto alive = alive_;
  std::thread([this, alive, data_ptr, names, snaps, wait_id, end_cb] {
    data_ptr->test_and_select(names);
    const BatchTestSummary sum = data_ptr->last_batch_summary();
    UiPostbox::instance().post([this, alive, data_ptr, snaps, end_cb, wait_id, sum] {
      if (alive && alive->load()) {
        data_ptr->restore_delays_for_still_pending(snaps);
        set_testing(false);
      }
      if (end_cb) end_cb(wait_id, sum);
    });
  }).detach();
}

void NodeList::restyle_theme() {
  if (search_) {
    search_->apply_theme();
    search_->style_.size = 11;
    search_->caret_color_ = search_->style_.color;
    // search_->radius_ 由 apply_theme() 按皮肤设置（4*g / 6*g / 8*g）
  }
  invalidate();
}

void NodeList::rebuild_visible() {
  // 懒加载兜底：bind 时若 profile 尚未激活（uid 空）会跳过读回，后续数据订阅/
  //   宽度变化触发重建、且 uid 已就绪时在此补读一次（← Mini use-head-state.ts:68-99
  //   挂载即 invoke 加载）。restore_head_state() 内部末尾也会回调本函数，但其进入
  //   前已把 head_state_loaded_ 置 true，故此处不会与之形成递归。
  if (!head_state_loaded_) restore_head_state();
  visible_.clear();
  if (!data_) return;
  const auto& rows = data_->rows();
  // Mini 无分组头（单组扁平列表，src: use-render-list.ts:144）
  std::vector<int> idx;
  idx.reserve(rows.size());
  for (size_t i = 0; i < rows.size(); ++i) {
    if (filter_.empty()) {
      idx.push_back(static_cast<int>(i));
      continue;
    }
    // name 子串大小写不敏感
    std::string lower;
    for (char c : rows[i].name)
      lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.find(filter_) != std::string::npos)
      idx.push_back(static_cast<int>(i));
  }
  if (sort_type_ == 1) {
    const auto& all_rows = data_->rows();
    std::stable_sort(idx.begin(), idx.end(), [this, &all_rows](int a, int b) {
      int da = data_->row_delay(all_rows[static_cast<size_t>(a)].name);
      int db = data_->row_delay(all_rows[static_cast<size_t>(b)].name);
      // src: use-filter-sort.ts:56-64「delay === 0 … return [3, timeout]」
      //   Timeout(0) 与 Unknown(-1)/testing(-2) 一并沉底；其余升序
      auto key = [](int d) { return d <= 0 ? 1 << 30 : d; };
      return key(da) < key(db);
    });
  } else if (sort_type_ == 2) {
    // src: use-filter-sort.ts:78 localeCompare
    std::stable_sort(idx.begin(), idx.end(), [&rows](int a, int b) {
      return locale_name_cmp(rows[static_cast<size_t>(a)].name,
                             rows[static_cast<size_t>(b)].name) < 0;
    });
  }
  visible_ = std::move(idx);
}

void NodeList::sync_list() {
  if (!data_ || !list_) return;
  auto* nd = data_;
  auto* self = this;
  const int cols = cols_ > 0 ? cols_ : 1;
  const int grid_rows =
      (static_cast<int>(visible_.size()) + cols - 1) / cols;

  auto build_row_items = [nd, self, cols](int index) {
    std::vector<GridCardItem> items;
    const int base = index * cols;
    const int total_vis = static_cast<int>(self->visible_.size());
    for (int c = 0; c < cols; ++c) {
      const int vi = base + c;
      if (vi >= total_vis) break;
      const int ri = self->visible_[vi];
      const auto& r = nd->rows()[static_cast<size_t>(ri)];
      items.push_back({r.type, r.name, nd->row_delay(r.name),
                       r.name == nd->now(), ri, vi + 1, total_vis});
    }
    return items;
  };

  std::vector<std::string> cur_names = visible_node_names();
  // BUG-339: 可见节点序列与列数未发生变化时，原地更新已物化卡片，不销毁重建子组件
  if (cols == last_synced_cols_ && cur_names == last_synced_names_ &&
      list_->item_count() == grid_rows) {
    for (int row_idx : list_->materialized_indices()) {
      if (auto* grid_row = dynamic_cast<NodeGridRowWidget*>(
              list_->materialized_row(row_idx))) {
        grid_row->update_cards_in_place(build_row_items(row_idx));
      }
    }
    const bool empty = visible_.empty();
    if (empty_icon_ && empty_icon_->visible() != empty) empty_icon_->set_visible(empty);
    if (empty_label_ && empty_label_->visible() != empty)
      empty_label_->set_visible(empty);
    list_->invalidate();
    if (scroll_top_) scroll_top_->set_shown(list_->scroll_offset() > kScrollTopShowPx);
    return;
  }

  last_synced_cols_ = cols;
  last_synced_names_ = std::move(cur_names);
  ++sync_rebuild_count_;

  list_->set_content(
      grid_rows, kNodeRowH,
      [self, cols, build_row_items](int index) {
        auto row = std::make_unique<NodeGridRowWidget>();
        row->on_select = [self](const std::string& name) {
          if (self->select_node) self->select_node(name);
        };
        row->on_test_single = [self](const std::string& name) {
          if (self->on_test_single) self->on_test_single(name);
        };
        row->set_cards(build_row_items(index), cols);
        return row;
      },
      15);  // src: src/components/proxy/proxy-groups.tsx:105
  const bool empty = visible_.empty();
  if (empty_icon_ && empty_icon_->visible() != empty) empty_icon_->set_visible(empty);
  if (empty_label_ && empty_label_->visible() != empty)
    empty_label_->set_visible(empty);
  list_->on_arrange(list_->bounds());
  if (scroll_top_) scroll_top_->set_shown(list_->scroll_offset() > kScrollTopShowPx);
}

bool NodeList::empty_state_visible() const {
  return empty_label_ && empty_label_->visible();
}

void NodeList::on_arrange(const RectF& area) {
  set_bounds(area);
  // 【坐标约定】子布局局部坐标。
  // 结构 ← proxy-groups.tsx:381-421：
  //   <Box className="theme-panel" boxSizing="border-box">  ← 外框（border: 4px double）
  //     <ProxyHead/>                                         ← 过滤栏，在框内
  //     <Box overflow:auto>…</Box>                           ← 滚动列表，在框内
  //   </Box>
  // 外层 margin ← proxy-groups.tsx:327 mx:1 / mb:1 → 左右各 8、底 8（顶部无外边距）。
  // 「4px double」在 border-box 下向内占 4px，故过滤栏与列表都要再内缩 kPanelBorder
  // ——此前把边框画在过滤栏下方、内容又从 x=0 起算，才出现「框和过滤栏分家」。
  const float inset_x = kListInset + kPanelBorder;                      // 12
  const float content_w = area.w - 2 * inset_x;                         // w-24
  const float content_top = kPanelBorder;                               // 4
  const float content_bottom = area.h - kListBottomGap - kPanelBorder;   // h-12
  const float list_top = content_top + kFilterBarH + kListTopGap;        // 43
  float list_h = content_bottom - list_top;
  if (list_h < 0) list_h = 0;

  // ★ ❌1 宽窗三列判定（← use-render-list.ts:88 isMinimalWidth → col 1/3）：
  //   内容宽 ≤285 单列，否则 3 列卡片墙；宽度/列数变化 → 重建网格
  const int new_cols = (content_w <= kMinWidthForMultiCol) ? 1 : 3;
  if (new_cols != cols_ || content_w != last_content_w_) {
    cols_ = new_cols;
    last_content_w_ = content_w;
    sync_list();
  }

  child(0)->layout_tree({inset_x, content_top, content_w, kFilterBarH});
  // 清空按钮为 TextField 内建，无需独立布局
  // 行内嵌金边列表容器（src: proxy-render.tsx:88-89 行 pl2 + 容器边框 1）
  list_->layout_tree({inset_x, list_top, content_w, list_h});

  // 空态居中于列表区：src: proxy-render.tsx:65-76（flex column 居中，py 16）
  if (empty_icon_ && empty_label_ && empty_icon_->visible()) {
    SizeF ls = empty_label_->measure(1e6f, list_h);
    const float icon_sz = 40.0f;  // 2.5em × 16px（src: proxy-render.tsx:75）
    const float total = icon_sz + ls.h;
    const float center_y = list_top + list_h / 2.0f;
    const float top = center_y - total / 2.0f;
    empty_icon_->layout_tree(
        {inset_x + (content_w - icon_sz) / 2.0f, top, icon_sz, icon_sz});
    empty_label_->layout_tree(
        {inset_x + (content_w - ls.w) / 2.0f, top + icon_sz, ls.w, ls.h});
  }

  // 滚动置顶按钮：absolute bottom 20 / right 20 —— 相对 proxy-groups.tsx:327 那个
  // 带 mx:1 / mb:1 的外层 Box，因此还要让开 8px 外边距
  if (scroll_top_) {
    scroll_top_->layout_tree(
        {area.w - kListInset - kScrollTopInset - kScrollTopBtnSize,
         area.h - kListBottomGap - kScrollTopInset - kScrollTopBtnSize,
         kScrollTopBtnSize, kScrollTopBtnSize});
  }
}

void NodeList::on_paint(IRenderTarget& rt) {
  const auto& svc = ThemeService::instance();
  const auto& vars = svc.vars();
  const bool light = svc.is_light();
  // 【UIFIX】局部坐标约定 {0,0,w,h}；底色 = list_bg（主题变量）
  const float w = bounds().w, h = bounds().h;
  if (search_) {
    search_->apply_theme();
    search_->style_.size = 11;
    search_->caret_color_ = search_->style_.color;
  }
  if (bolt_btn_) bolt_btn_->color_ = svc.palette().primary_text;
  if (sort_btn_) sort_btn_->color_ = svc.palette().primary_text;
  if (empty_icon_) empty_icon_->color_ = svc.palette().primary_text;
  if (empty_label_) empty_label_->style.color = svc.palette().primary_text;
  rt.fill_rect({0, 0, w, h}, Brush{vars.list_bg});
  // theme-panel 外框 ← src: proxy-groups.tsx:383「className="theme-panel"」+
  //   index.scss:75「border: 4px double var(--theme-border)」/:125「4px double #B8860B」
  //   范围 = 外层 mx:1 / mb:1 之内、顶部贴齐 → 过滤栏与列表同在一个框里。
  const RectF panel{kListInset, 0, w - 2 * kListInset, h - kListBottomGap};
  // 6 套皮肤的容器差异全部来自 ThemeService::panel_paint()
  // （index.scss:74-84 基线 + :123-329 各皮肤 override；随 depth/vibrancy 变化）
  const theme::PanelPaint pp = svc.panel_paint();

  // ① 外阴影 / 外发光：先于本体绘制，避免压住边框
  if (pp.has_shadow)
    draw_soft_shadow(rt, panel, pp.radius, pp.shadow_color, pp.shadow_dy,
                     pp.shadow_blur);
  if (pp.has_glow)
    draw_glow(rt, panel, pp.radius, pp.glow_color, pp.glow_blur);

  // ② 本体背景：retro-3d 为 135° 三停渐变，其余为纯色/半透明
  if (pp.grad_bg) {
    rt.fill_round_rect_gradient_angle(panel, pp.radius, pp.grad, pp.grad_count,
                                      pp.grad_angle);
  } else if (pp.bg.a > 0.001f) {
    Brush bg_brush{pp.bg};
    rt.round_rect(panel, pp.radius, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &bg_brush);
  }

  // ②' 内浮雕（← retro-3d 的两层 inset boxShadow：左上高光 / 右下暗边）
  if (pp.has_inner_bevel) {
    const float ih = 3.0f;  // 贴着 double border 内沿
    rt.draw_line({panel.x + pp.radius, panel.y + ih},
                 {panel.right() - pp.radius, panel.y + ih},
                 Pen{pp.inner_hi, 1.0f});
    rt.draw_line({panel.x + ih, panel.y + pp.radius},
                 {panel.x + ih, panel.bottom() - pp.radius},
                 Pen{pp.inner_hi, 1.0f});
    rt.draw_line({panel.x + pp.radius, panel.bottom() - ih},
                 {panel.right() - pp.radius, panel.bottom() - ih},
                 Pen{pp.inner_dark, 1.0f});
    rt.draw_line({panel.right() - ih, panel.y + pp.radius},
                 {panel.right() - ih, panel.bottom() - pp.radius},
                 Pen{pp.inner_dark, 1.0f});
  }

  // 工具条底边 1px palette.divider（ProxyHead 的 borderBottom，位于框内）
  // src: proxy-head.tsx:75 borderBottom 1px divider；proxy-groups.tsx:397
  const Color4f bar_divider =
      light ? Color4f(0, 0, 0, 0.12f) : Color4f(1, 1, 1, 0.12f);
  rt.fill_rect({panel.x + kPanelBorder, kPanelBorder + kFilterBarH - 1,
                panel.w - 2 * kPanelBorder, 1},
               Brush{bar_divider});

  // ③ 边框：4px double = 两道 1px 环（src: index.scss:75/125）
  //    cyberpunk 的呼吸边框按 vibrancy 决定的周期在 from→to 之间往返
  if (pp.border == theme::PanelPaint::BorderKind::None) return;
  Color4f bc = pp.border_color;
  if (pp.pulse) {
    // 与极光层同一时间基准：进程启动毫秒数（GetTickCount64，windows.h 已引入）
    const float now_s = static_cast<float>(GetTickCount64()) / 1000.0f;
    bc = Color4f::lerp(pp.pulse_from, pp.pulse_to, svc.pulse_phase(now_s));
    invalidate();
  }
  // 线宽取 1.34 逻辑 px ≈ 2 物理 px：CSS「4px double」的单边在 1.5× DPI 下正好是
  //   2 物理 px 满色；若按 1 逻辑 px 画，D2D 抗锯齿会把它摊成两半，观感明显偏暗。
  Pen border{bc, 1.34f};
  rt.round_rect(panel, pp.radius, border, nullptr);
  const float inner_r = pp.radius > 3.0f ? pp.radius - 3.0f : 0.0f;
  rt.round_rect({panel.x + 3, panel.y + 3, panel.w - 6, panel.h - 6}, inner_r,
                border, nullptr);
}

}  // namespace clashwin::ui::scene

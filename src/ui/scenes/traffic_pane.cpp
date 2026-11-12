// 流量面板实现（T14）。数值全部 src 标注（retro-3d 默认皮肤，button-styles.ts:11）。
#include "traffic_pane.h"

#include <windows.h>

#include <cmath>
#include <memory>

#include "../card_draw.h"
#include "../theme/theme.h"
#include "util/log.h"
#include "util/parse_traffic.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

using theme::ThemeService;

namespace {
int64_t default_now_ms() { return static_cast<int64_t>(GetTickCount64()); }

inline bool light_mode() { return ThemeService::instance().is_light(); }

// src: _layout.tsx:1774 padding: 宽窗 '8px 12px 2px 12px' / 窄窗 '3px 6px 2px 6px'
constexpr float kPadTopWide = 8.0f, kPadXWide = 12.0f;
constexpr float kPadTopNarrow = 3.0f, kPadXNarrow = 6.0f;
constexpr float kPadBottom = 2.0f;
// src: _layout.tsx:1776 gap: isMinimalWidth ? '6px' : 0
constexpr float kGapNarrow = 6.0f;
// src: mini-traffic-panel.tsx:71-79 宽窗指标行：h22 / mt 0.5 / pt 0.5 / px 1 / gap 1
constexpr float kMetricsH = 22.0f;
constexpr float kMetricsMtWide = 4.0f;
constexpr float kMetricsPtWide = 4.0f;
constexpr float kMetricsPxWide = 8.0f;
constexpr float kMetricsGapWide = 8.0f;
// src: mini-traffic-panel.tsx:100 卡内 px 0.5 = 4px
constexpr float kCardPadX = 4.0f;
// src: mini-traffic-panel.tsx:76 mt 0.25=2px；:78 px 0.25=2px；:81-86 窄窗
//   column/gap 0；:99 卡高 18px
constexpr float kCardH = 18.0f, kRowMtop = 2.0f, kRowPx = 2.0f;
constexpr float kCardR = 6.0f;    // src: button-styles.ts:549 borderRadius 6px
constexpr float kRunGap = 2.4f;   // src: mini-traffic-panel.tsx:98 gap 0.3=2.4px
constexpr float kIconBox = 14.0f;  // src: mini-traffic-panel.tsx:114 fontSize 14

// 卡底/描边/阴影：全部由 ThemeService::card_paint("download"/"upload") 给出
// （4 卡型 × 6 皮肤查表，见 theme.cpp）。此处不再有手写金色渐变。

// 文字/图标色（src: mini-traffic-panel.tsx；--download-text/--upload-text 仅
// original 皮肤定义 index.scss:306-312，其余皮肤走 CSS var fallback 字面值）。
// var(--download-text, fallback) 的取值规则：original 命中变量、其余命中 fallback。
inline Color4f dl_var(bool dark, uint32_t fallback) {  // index.scss:306-307
  if (ThemeService::instance().skin() != theme::SkinId::Original)
    return Color4f::hex(fallback);
  const float v = ThemeService::instance().vibrancy_factor();
  return theme::hsl_to_rgb(200.0f * v, 1.0f, dark ? 0.75f : 0.25f);
}
inline Color4f ul_var(bool dark, uint32_t fallback) {  // index.scss:311-312
  if (ThemeService::instance().skin() != theme::SkinId::Original)
    return Color4f::hex(fallback);
  const float v = ThemeService::instance().vibrancy_factor();
  return theme::hsl_to_rgb(38.0f * v, 1.0f, dark ? 0.75f : 0.30f);
}

inline Color4f dl_icon_color() {   // :111-113
  return dl_var(!light_mode(), light_mode() ? 0x0084FF : 0x80D8FF);
}
inline Color4f ul_icon_color() {   // :240-241
  return ul_var(!light_mode(), light_mode() ? 0xE65100 : 0xFFD54F);
}
inline Color4f dl_text_color() {   // prefix/unit :121-123,150-152
  return dl_var(!light_mode(), light_mode() ? 0x006064 : 0x80D8FF);
}
inline Color4f dl_value_color() {  // :138-140（暗态写死 #FFFFFF，不走变量）
  return light_mode() ? dl_var(false, 0x00363A) : Color4f::hex(0xFFFFFF);
}
inline Color4f dl_unit_color() {   // :150-152
  return dl_var(!light_mode(), light_mode() ? 0x006064 : 0xE0F7FA);
}
inline Color4f ul_text_color() {   // prefix :250-251
  return ul_var(!light_mode(), light_mode() ? 0x7B5200 : 0xFFD54F);
}
inline Color4f ul_value_color() {  // :266（暗态写死 #FFFFFF）
  return light_mode() ? ul_var(false, 0x3E2723) : Color4f::hex(0xFFFFFF);
}
inline Color4f ul_unit_color() {   // :277-278
  return ul_var(!light_mode(), light_mode() ? 0x7B5200 : 0xFFECB3);
}

// ArrowDownward/UpwardRounded 近似（src: :108/:236 fontSize 14）
void draw_arrow(IRenderTarget& rt, const PointF& c, bool down,
                const Color4f& col) {
  Pen p{col, 1.5f};
  if (down) {
    rt.draw_line({c.x, c.y - 5.0f}, {c.x, c.y + 4.0f}, p);
    rt.draw_line({c.x - 3.5f, c.y}, {c.x, c.y + 4.0f}, p);
    rt.draw_line({c.x + 3.5f, c.y}, {c.x, c.y + 4.0f}, p);
  } else {
    rt.draw_line({c.x, c.y + 5.0f}, {c.x, c.y - 4.0f}, p);
    rt.draw_line({c.x - 3.5f, c.y}, {c.x, c.y - 4.0f}, p);
    rt.draw_line({c.x + 3.5f, c.y}, {c.x, c.y - 4.0f}, p);
  }
}
}  // namespace

TrafficSeries::TrafficSeries() = default;

void TrafficSeries::append(double up, double down) {
  points_.emplace_back(up, down);
  if (points_.size() > kTrafficMaxPoint + 2) points_.pop_front();  // src: traffic-graph.tsx:22,75
  double v = std::max(up, down);
  if (v > peak_) peak_ = v;
}

std::vector<float> TrafficSeries::up() const {
  std::vector<float> v;
  v.reserve(points_.size());
  for (auto& p : points_) v.push_back(static_cast<float>(p.first));
  return v;
}

std::vector<float> TrafficSeries::down() const {
  std::vector<float> v;
  v.reserve(points_.size());
  for (auto& p : points_) v.push_back(static_cast<float>(p.second));
  return v;
}

bool TrafficSeries::has_traffic() const {  // src: traffic-graph.tsx:24-27
  for (auto& p : points_)
    if (p.first != 0 || p.second != 0) return true;
  return false;
}

TrafficPane::TrafficPane(Widget* parent) : Widget(parent) {
  now_ms_ = [] { return static_cast<int64_t>(GetTickCount64()); };
  auto graph = std::make_unique<CanvasGraph>();
  graph_ = graph.get();
  graph_->flex_weight = 1;  // src: mini-traffic-panel.tsx:58 flex 1
  // 折线色 = 主题 token（src: traffic-graph.tsx:122-123 down=primary.main
  //   up=secondary.main）；α：down .9(:14) up .8(:11)。on_paint 每帧刷新。
  graph_->bg_ = Color4f(0, 0, 0, 0);  // src: traffic-graph.tsx:163 仅 clearRect，画布透明
  graph_->set_a11y_hidden(true);
  add_child(std::move(graph));

  // 12 个文字段（src: mini-traffic-panel.tsx 每卡 prefix 9px bold(:119) +
  //   value 13px bold(:136) + unit 8px normal(:147)）
  auto mk = [&](Label*& slot, float size, bool bold) {
    auto lb = std::make_unique<Label>();
    slot = lb.get();
    slot->style.size = size;
    slot->style.weight = bold ? static_cast<int>(Weight::Bold)
                              : static_cast<int>(Weight::Normal);
    slot->style.align_v = AlignV::Center;
    slot->style.wrap = false;
    slot->set_a11y_hidden(true);
    add_child(std::move(lb));
  };
  mk(dl_prefix_, 9, true); mk(dl_value_, 13, true); mk(dl_unit_, 8, false);
  mk(dt_prefix_, 9, true); mk(dt_value_, 13, true); mk(dt_unit_, 8, false);
  mk(ul_prefix_, 9, true); mk(ul_value_, 13, true); mk(ul_unit_, 8, false);
  mk(ut_prefix_, 9, true); mk(ut_value_, 13, true); mk(ut_unit_, 8, false);
  graph_->set_series(series_.up(), series_.down(), 1.0f);  // 初值底线（对齐 Mini 32 零点基线）
  refresh_labels();  // 初值即显（Mini：无流量也显示 0.00 B/s）
}

TrafficPane::~TrafficPane() = default;

bool TrafficPane::on_traffic_payload(const std::string& payload_text) {
  try {
    return on_traffic_json(nlohmann::json::parse(payload_text));
  } catch (const std::exception&) {
    return false;
  }
}

bool TrafficPane::on_traffic_json(const nlohmann::json& j) {
  if (!j.is_object() || !j.contains("up") || !j.contains("down")) return false;

  // src: traffic-graph.tsx:65-79 + use-traffic-data.ts:9-28
  // 采样周期与 Mini 保持一致（1000ms 采样步长，放宽抖动容限至 500ms 防丢弃 995ms 合法包）
  int64_t now = now_ms_ ? now_ms_() : 0;
  double up = j.value("up", 0.0);
  double down = j.value("down", 0.0);
  down_total_ = static_cast<double>(j.value("downTotal", 0.0));
  up_total_ = static_cast<double>(j.value("upTotal", 0.0));
  constexpr int64_t kMinIntervalMs = 500;
  if (last_sample_ms_ >= 0 && now - last_sample_ms_ < kMinIntervalMs)
    return true;  // 过滤 <500ms 重复包/突发包
  last_sample_ms_ = now;

  bool retained = series_.has_traffic();
  if (up == 0 && down == 0 && !retained) return true;  // 零流量且无历史：不入窗
  series_.append(up, down);
  if (!enabled_) return true;  // 数据已入窗，跳过重绘

  graph_->set_series(series_.up(), series_.down(),
                     static_cast<float>(std::max(series_.peak(), 1.0)));
  refresh_labels();
  invalidate();  // 窗口失焦时仍持续驱动帧渲染动画（对齐 Mini）
  for (auto& [id, cb] : subs_) cb();
  return true;
}

void TrafficPane::set_enabled(bool on) {
  if (enabled_ == on) return;
  enabled_ = on;
  if (!on) return;  // 失焦：保留数据，停止重绘
  // 回到前台立即用已累计的序列补一次绘制，避免恢复瞬间仍显示旧值
  if (graph_) {
    graph_->set_series(series_.up(), series_.down(),
                       static_cast<float>(std::max(series_.peak(), 1.0)));
  }
  refresh_labels();
  for (auto& [id, cb] : subs_) cb();
}

void TrafficPane::refresh_labels() {
  // ← parse_traffic（T02）：值+单位两段（窄字符 → 宽转换）
  auto wide = [](const std::string& s) {
    return clashwin::utf::utf8_to_utf16(s);
  };
  auto [dv, du] = clashwin::util::parse_traffic(
      series_.size() ? series_.down().back() : 0.0f);
  auto [uv, uu] = clashwin::util::parse_traffic(
      series_.size() ? series_.up().back() : 0.0f);
  auto [dtv, dtu] = clashwin::util::parse_traffic(down_total_);
  auto [utv, utu] = clashwin::util::parse_traffic(up_total_);
  // src: mini-traffic-panel.tsx:128-157 下载速率卡「下载: 值 单位/s」
  dl_prefix_->set_text(L"下载:");
  dl_value_->set_text(wide(dv));
  dl_unit_->set_text(wide(du) + L"/s");
  // :176-213 下载总量卡「总量: 值 单位」（无 /s）
  dt_prefix_->set_text(L"总量:");
  dt_value_->set_text(wide(dtv));
  dt_unit_->set_text(wide(dtu));
  // :245-283 上传速率卡
  ul_prefix_->set_text(L"上传:");
  ul_value_->set_text(wide(uv));
  ul_unit_->set_text(wide(uu) + L"/s");
  // :302-337 上传总量卡
  ut_prefix_->set_text(L"总量:");
  ut_value_->set_text(wide(utv));
  ut_unit_->set_text(wide(utu));
  down_text_ = wide(dv) + L" " + wide(du) + L"/s";
  up_text_ = wide(uv) + L" " + wide(uu) + L"/s";
}

void TrafficPane::set_mini(bool mini) {
  if (mini_ == mini) return;
  mini_ = mini;
  // src: _layout.tsx:1764-1782 迷你窗同样渲染 MiniTrafficPanel（四卡常驻），
  //   仅面板总高改为 TRAFFIC_PANE_HEIGHT_MINIMAL=100；不隐藏任何指标卡
  invalidate();
}

void TrafficPane::set_narrow(bool narrow) {
  if (narrow_ == narrow) return;
  narrow_ = narrow;
  invalidate();
}

void TrafficPane::on_arrange(const RectF& area) {
  set_bounds(area);
  // 【坐标约定】widget 树 = 逻辑 DIP（src 的 CSS px 直接用）
  const float w = area.w;
  const float h = area.h;
  // src: _layout.tsx:1774/1776 宽窄分支 padding + gap（窄窗 gap 6 计入图高损耗）
  const float pad_top = narrow_ ? kPadTopNarrow : kPadTopWide;
  const float pad_x = narrow_ ? kPadXNarrow : kPadXWide;
  const float gap = narrow_ ? kGapNarrow : 0.0f;
  // src: mini-traffic-panel.tsx:71-87 宽窗(>560) 单行 22px 横排；
  //   ≤560/窄窗走 column，宽窗 pt 0.5 另加 4px
  const bool horiz = !narrow_ && w > 560.0f;
  const float mt = narrow_ ? kRowMtop : kMetricsMtWide;
  const float pt = narrow_ ? 0.0f : kMetricsPtWide;
  const float rows_h = gap + mt + pt + (horiz ? kMetricsH : 2 * kCardH);
  graph_rect_ = {pad_x, pad_top, w - 2 * pad_x,
                 h - pad_top - kPadBottom - rows_h};
  child(0)->layout_tree(graph_rect_);
  metrics_border_y_ = horiz || !narrow_ ? graph_rect_.bottom() + mt : -1.0f;

  const float ul_y = h - kPadBottom - kCardH;
  const float dl_y = ul_y - kCardH;
  const float px = narrow_ ? kRowPx : kMetricsPxWide;
  const float x0 = pad_x + px;
  if (horiz) {
    // 四卡单行：下载组[速率|总量] + 8px + 上传组[速率|总量]
    const float cw = (w - 2 * x0 - kMetricsGapWide) / 4.0f;
    const float row_y = graph_rect_.bottom() + mt + pt;
    dl_l_ = {x0, row_y, cw, kCardH};
    dl_r_ = {x0 + cw, row_y, cw, kCardH};
    ul_l_ = {x0 + 2 * cw + kMetricsGapWide, row_y, cw, kCardH};
    ul_r_ = {x0 + 3 * cw + kMetricsGapWide, row_y, cw, kCardH};
  } else {
    // 卡行：下载行在上、上传行贴底（:81-86 column；组间 gap 0）
    const float cw = (w - 2 * x0) / 2.0f;  // 两卡 flex 1 等分（:97）
    dl_l_ = {x0, dl_y, cw, kCardH};
    dl_r_ = {x0 + cw, dl_y, cw, kCardH};
    ul_l_ = {x0, ul_y, cw, kCardH};
    ul_r_ = {x0 + cw, ul_y, cw, kCardH};
  }

  // 卡内：justifyContent center（:95-96），段间 gap 2.4（:98）；速率卡带图标
  auto place = [&](Label* p1, Label* p2, Label* p3, const RectF& card,
                   PointF* icon) {
    float gw = 0;
    float widths[3] = {p1->measure(0, 0).w, p2->measure(0, 0).w,
                       p3->measure(0, 0).w};
    for (float x : widths) gw += x;
    gw += 2 * kRunGap;
    if (icon) gw += kIconBox + kRunGap;
    float cx = card.x + (card.w - gw) / 2.0f;
    if (icon) {
      *icon = {cx + kIconBox / 2.0f, card.y + card.h / 2.0f};
      cx += kIconBox + kRunGap;
    }
    Label* lbs[3] = {p1, p2, p3};
    for (int i = 0; i < 3; ++i) {
      lbs[i]->layout_tree({cx, card.y, widths[i] + 2, card.h});
      cx += widths[i] + kRunGap;
    }
  };
  place(dl_prefix_, dl_value_, dl_unit_, dl_l_, &dl_icon_);
  place(dt_prefix_, dt_value_, dt_unit_, dl_r_, nullptr);
  place(ul_prefix_, ul_value_, ul_unit_, ul_l_, &ul_icon_);
  place(ut_prefix_, ut_value_, ut_unit_, ul_r_, nullptr);
}

void TrafficPane::on_paint(IRenderTarget& rt) {
  // 折线/参考线色随主题刷新
  // src: traffic-graph.tsx:122-123 down=primary.main α.9(:14) up=secondary.main α.8(:11)
  //   :121+MUI 默认 divider=rgba(0,0,0,.12)/rgba(255,255,255,.12) × α.15(:8)
  if (graph_) {
    const auto& pal = ThemeService::instance().palette();
    auto mul = [](Color4f c, float a) { c.a *= a; return c; };
    graph_->down_color_ = mul(pal.primary_color, 0.9f);
    graph_->up_color_ = mul(pal.secondary_color, 0.8f);
    graph_->line_ = light_mode() ? Color4f(0, 0, 0, 0.018f)
                                 : Color4f(1, 1, 1, 0.018f);
  }
  // 面板底：无论是否有有效图宽，先绘制 theme_bg 保证底栏 100% 不透明，杜绝透底
  rt.fill_rect({0, 0, bounds().w, bounds().h},
               Brush{ThemeService::instance().vars().theme_bg});

  if (graph_rect_.w <= 0) return;

  const bool light = light_mode();
  const float pad_x = narrow_ ? kPadXNarrow : kPadXWide;
  if (!narrow_ && metrics_border_y_ >= 0) {
    // src: mini-traffic-panel.tsx:74-75 borderTop 1px divider × depth-factor 1
    rt.fill_rect({pad_x, metrics_border_y_, bounds().w - 2 * pad_x, 1},
                 Brush{light ? Color4f(0, 0, 0, 0.12f)
                             : Color4f(1, 1, 1, 0.12f)});
  }
  if (!narrow_) {
    // src: :77-80 bgcolor action.hover + borderRadius 1(=8px)
    Pen none{Color4f(0, 0, 0, 0), 0};
    Brush bg{light ? Color4f(0, 0, 0, 0.04f) : Color4f(1, 1, 1, 0.08f)};
    rt.round_rect(graph_rect_, kCardR + 2, none, &bg);
  }

  // 成对卡（:102-104/:171-173 内角半径 0 → 左卡 TL|BL、右卡 TR|BR）
  // 底色/渐变/描边/阴影/发光全部来自 card_paint（4 卡型 × 6 皮肤，theme.cpp）
  const theme::CardPaint dcp = ThemeService::instance().card_paint("download");
  const theme::CardPaint ucp = ThemeService::instance().card_paint("upload");
  const float dl_r = dcp.radius, ul_r = ucp.radius;
  draw_card(rt, dl_l_, dcp, 0x9);  // bit0 TL | bit3 BL
  draw_card(rt, dl_r_, dcp, 0x6);  // bit1 TR | bit2 BR
  draw_card(rt, ul_l_, ucp, 0x9);
  draw_card(rt, ul_r_, ucp, 0x6);
  // 底缘 bevel / 内高光：retro-3d 专属（src: button-styles.ts:556-565 boxShadow
  //   顶部 1px 内高光 + 底缘 1-2px 硬 bevel）。其余皮肤该层 boxShadow 已由
  //   card_paint 的 has_shadow/glow 表达，这里只在 retro 下补描边细节。
  const bool retro = ThemeService::instance().skin() == theme::SkinId::Retro3d;
  if (retro) {
    Pen dl_pen{dcp.shadow_color, 1.0f}, ul_pen{ucp.shadow_color, 1.0f};
    rt.draw_line({dl_l_.x + dl_r, dl_l_.bottom() - 0.5f},
                 {dl_r_.right() - dl_r, dl_l_.bottom() - 0.5f}, dl_pen);
    rt.draw_line({ul_l_.x + ul_r, ul_l_.bottom() - 0.5f},
                 {ul_r_.right() - ul_r, ul_l_.bottom() - 0.5f}, ul_pen);
    auto decorate = [&](const RectF& l, const RectF& r, float rad,
                        Color4f bevel) {
      const float top_y = l.y + 1.0f;
      rt.draw_line(
          {l.x + rad, top_y}, {r.right() - rad, top_y},
          Pen{light ? Color4f(1, 1, 1, 0.80f) : Color4f(1, 1, 1, 0.08f), 1.0f});
      for (int i = 1; i <= 2; ++i) {
        Color4f b = bevel;
        b.a = 0.45f / static_cast<float>(i);
        rt.draw_line({l.x + rad, l.bottom() - 0.5f + i},
                     {r.right() - rad, l.bottom() - 0.5f + i}, Pen{b, 1.0f});
      }
      for (int i = 1; i <= 2; ++i) {
        Color4f b = bevel;
        b.a = 0.35f / static_cast<float>(i);
        rt.draw_line({l.x + rad, l.bottom() + 2.0f + i},
                     {r.right() - rad, l.bottom() + 2.0f + i}, Pen{b, 1.0f});
      }
    };
    decorate(dl_l_, dl_r_, dl_r, dcp.shadow_color);
    decorate(ul_l_, ul_r_, ul_r, ucp.shadow_color);
  }

  // 图标（src: :108-116 下载箭头 / :236-244 上传箭头；总量卡无图标）
  draw_arrow(rt, dl_icon_, true, dl_icon_color());
  draw_arrow(rt, ul_icon_, false, ul_icon_color());

  // 文字段色随主题（Label 子件在 on_paint 后绘制，此处刷新即生效）
  auto setc = [](Label* lb, Color4f c) { lb->style.color = c; };
  setc(dl_prefix_, dl_text_color()); setc(dl_value_, dl_value_color());
  setc(dl_unit_, dl_unit_color());
  setc(dt_prefix_, dl_text_color()); setc(dt_value_, dl_value_color());
  setc(dt_unit_, dl_unit_color());
  setc(ul_prefix_, ul_text_color()); setc(ul_value_, ul_value_color());
  setc(ul_unit_, ul_unit_color());
  setc(ut_prefix_, ul_text_color()); setc(ut_value_, ul_value_color());
  setc(ut_unit_, ul_unit_color());
}

uint64_t TrafficPane::subscribe(std::function<void()> cb) {
  uint64_t id = next_id_++;
  subs_.emplace_back(id, std::move(cb));
  return id;
}

void TrafficPane::unsubscribe(uint64_t id) {
  subs_.erase(std::remove_if(subs_.begin(), subs_.end(),
                             [id](const auto& p) { return p.first == id; }),
              subs_.end());
}

std::wstring TrafficPane::a11y_default_name() const {
  auto fmt_card = [](const Label* p, const Label* v, const Label* u) {
    if (!p || !v || !u) return std::wstring{};
    return p->text + L" " + v->text + L" " + u->text;
  };
  return fmt_card(dl_prefix_, dl_value_, dl_unit_) + L", " +
         fmt_card(dt_prefix_, dt_value_, dt_unit_) + L", " +
         fmt_card(ul_prefix_, ul_value_, ul_unit_) + L", " +
         fmt_card(ut_prefix_, ut_value_, ut_unit_);
}

}  // namespace clashwin::ui::scene

// 快捷条实现（T13）。数值/配色全部取自 Clash Mini 3.2.2 源码（见各处 // src:）。
#include "quick_bar.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <map>

#include "../theme/theme.h"
#include "../soft_shadow.h"  // 卡面阴影/发光近似（随 depth/vibrancy）
#include "node_list.h"  // delay_tier / delay_tier_color（与节点表格同源五档色）
#include "util/log.h"
#include "util/utf.h"
#include "ui/i18n_text.h"  // F-D01

namespace clashwin::ui::scene {

using theme::ThemeService;  // T12 主题服务（选中卡/文字色随深浅主题）

namespace {
// steady_clock 毫秒（动画过渡计时；避免依赖 GetTickCount 的 49.7 天回绕）
double steady_ms() {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// src: settings.json:541-543 settings.mini（手动模式/系统代理/TUN 模式）
inline std::wstring takeover_label(int i) {
  static const char* kKeys[] = {"settings.mini.manual", "settings.mini.system",
                               "settings.mini.tun"};
  return tr(kKeys[i]);
}
inline const char* takeover_mode(int i) {
  static const char* kModes[] = {"MAN", "SYS", "TUN"};
  return kModes[i];
}

// 协议名映射：src: style-helpers.tsx:202-219（未命中则原串转大写）
std::wstring friendly_protocol(const std::string& type) {
  if (type.empty()) return L"";
  std::wstring low = utf::utf8_to_utf16(type);
  for (auto& c : low) c = static_cast<wchar_t>(std::towlower(c));
  static const std::map<std::wstring, std::wstring> kMap = {
      {L"ss", L"Shadowsocks"},        {L"ssr", L"ShadowsocksR"},
      {L"vmess", L"VMess"},           {L"vless", L"VLESS"},
      {L"trojan", L"Trojan"},         {L"hysteria", L"Hysteria"},
      {L"hysteria2", L"Hysteria 2"},  {L"tuic", L"TUIC"},
      {L"wireguard", L"WireGuard"},   {L"shadowsocks", L"Shadowsocks"},
      {L"shadowsocksr", L"ShadowsocksR"}};
  auto it = kMap.find(low);
  if (it != kMap.end()) return it->second;
  for (auto& c : low) c = static_cast<wchar_t>(std::towupper(c));
  return low;
}

// retro-3d 暗态：src: theme-settings-card.tsx:57 skin==='retro-3d' && mode==='dark'
inline bool is_retro3d_dark() {
  return ThemeService::instance().skin() == theme::SkinId::Retro3d &&
         !ThemeService::instance().is_light();
}

// 选中卡卡面：6 套皮肤统一取自 ThemeService::card_paint("default")
// 出处：utils/button-styles.ts:492-731 get3DCardStyle('default')。
// 旧实现只画 retro-3d 的浅黄渐变，其它皮肤下这张卡仍是金色 —— 已修正。
void paint_card_gradient(IRenderTarget& rt, const RectF& r) {
  const theme::CardPaint cp =
      ThemeService::instance().card_paint("default");
  if (cp.has_shadow)
    draw_soft_shadow(rt, r, cp.radius, cp.shadow_color, cp.shadow_dy,
                     cp.shadow_blur);
  if (cp.has_glow) draw_glow(rt, r, cp.radius, cp.glow_color, cp.glow_blur);
  if (cp.grad_bg) {
    rt.fill_round_rect_gradient_angle(r, cp.radius, cp.grad, cp.grad_count,
                                      cp.grad_angle);
  } else if (cp.bg.a > 0.001f) {
    Brush bg{cp.bg};
    rt.round_rect(r, cp.radius, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &bg);
  }
  if (cp.border_w > 0.0f) {
    rt.round_rect(r, cp.radius, Pen{cp.border, cp.border_w}, nullptr);
  }
}

// 选中卡正文色：随皮肤取自 card_paint().text_color
// （retro-3d light #3C2F0F / dark #2C1F03；original/monochrome 黑白对调…）
inline Color4f card_ink() {
  return ThemeService::instance().card_paint("default").text_color;
}
// 选中卡 bevel 暗边：src: button-styles.ts:540（light #B8860B / dark #755500）
inline Color4f card_bevel_dark() {
  return Color4f::hex(ThemeService::instance().is_light() ? 0xB8860B : 0x755500);
}

// quickbar 右侧 pin/gear 的 3D 按钮底：Mini 用 IconButton + get3DButtonStyle
// src: _layout.tsx:1338-1348（pin，contained/default 或 pinned→primary）、
//      :1375-1381（gear，contained/default）
// T12：改为 ThemeService::button_paint() 查表（6 套皮肤 × 亮暗 × 状态），
//   此前写死 retro-3d 金色，切皮肤后这两个按钮仍是金色 —— 是最显眼的残留。
void paint_button_bg(IRenderTarget& rt, const RectF& r, bool primary, bool hover,
                     bool pressed) {
  auto& svc = ThemeService::instance();
  const auto p = svc.button_paint(primary ? "primary" : "default");
  // 外阴影 + 外发光（← boxShadow 常规层与 0 0 N 光晕）
  if (p.has_shadow) draw_soft_shadow(rt, r, p.radius, p.shadow_color,
                                     p.shadow_dy, p.shadow_blur);
  if (p.glow.a > 0) draw_glow(rt, r, p.radius, p.glow, p.glow_blur);
  // 主体
  if (p.use_grad) {
    const GradientStop* gs = hover ? p.grad_hover : p.grad;
    rt.fill_round_rect_gradient_angle(r, p.radius, gs, p.grad_count, p.grad_angle);
  } else {
    Color4f fill = pressed ? p.fill_pressed : (hover ? p.fill_hover : p.fill);
    Brush fb{fill};
    rt.round_rect(r, p.radius, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &fb);
  }
  if (p.border_w > 0.01f)
    rt.round_rect(r, p.radius, Pen{p.border, p.border_w}, nullptr);
  // retro 浮雕：顶部内高光 + 底部 bevel 暗线（← :89-96 多层 boxShadow）
  if (p.use_grad && p.bevel_dark.a > 0) {
    rt.draw_line({r.x + p.radius, r.y + 1.0f},
                 {r.right() - p.radius, r.y + 1.0f}, Pen{p.bevel_hi, 1.0f});
    rt.draw_line({r.x + p.radius, r.bottom() - 1.0f},
                 {r.right() - p.radius, r.bottom() - 1.0f},
                 Pen{p.bevel_dark, 1.0f});
  }
  // hover/active 叠加（← button-styles.ts 各皮肤 &:hover / &:active）
  if (hover || pressed) {
    Pen none{Color4f(0, 0, 0, 0), 0};
    Brush ov{1, 1, 1, pressed ? 0.04f : 0.12f};
    rt.round_rect(r, p.radius, none, &ov);
  }
  if (pressed) {
    Pen none{Color4f(0, 0, 0, 0), 0};
    Brush ov{0, 0, 0, 0.10f};
    rt.round_rect({r.x + 2, r.y + 2, r.w - 4, r.h - 4}, p.radius, none, &ov);
    rt.fill_rect({r.x + 2, r.y + 2, r.w - 4, 1},
                 Brush{Color4f(1, 1, 1, 0.50f)});
  }
}
}  // namespace

// ---------- QuickBar ----------
QuickBar::QuickBar(Widget* parent) : Row(parent) {
  set_padding(8, 0, 8, 2);  // src: _layout.tsx:1317 padding '0px 8px 2px 8px'
  spacing = 0;              // src: _layout.tsx:1321 gap 0（卡片占满，右侧 pin/gear 紧贴）
  cross_align = CrossAlign::Center;

  // 「活跃节点：」标签（caption 11px；≤580 隐藏）
  // src: active-node-card.tsx:247-260、locales/zh/settings.json:557
  auto act = std::make_unique<Label>();
  active_label_ = act.get();
  active_label_->style.size = 11;  // src: :251
  active_label_->set_text(tr("settings.mini.activeNodeLabel"));
  add_child(std::move(act));

  // 协议胶囊文字（11px 粗体 + 1px 边框 + 圆角 4 + px6.4/py1.6；≤480 隐藏）
  // src: active-node-card.tsx:262-279（缺省 'Direct'）
  auto proto = std::make_unique<Label>();
  proto_label_ = proto.get();
  proto_label_->style.size = 11;                            // src: :266
  proto_label_->style.weight = static_cast<int>(Weight::Bold);  // src: :265
  proto_label_->set_text(L"Direct");  // src: :278 「|| 'Direct'」
  add_child(std::move(proto));

  // 当前节点名（12px 粗体、单行省略）
  // src: active-node-card.tsx:283-308（body2 12px bold；maxWidth 180 + minWidth 0
  //   + overflow hidden + textOverflow ellipsis → 名称按内容宽、空间不足时省略）
  // 【P0-quickbar】不再 flex 撑满：此前 name 吃掉全部剩余空间，延迟徽章被推到
  //   卡片右缘、视觉上“挤出”卡片；改为名称按文字宽（可收缩省略）+ 延迟固定宽，
  //   间距取 Paper gap 1.5=12px（active-node-card.tsx:240）。
  auto label = std::make_unique<Label>();
  node_label_ = label.get();
  node_label_->style.size = 12;
  node_label_->style.weight = static_cast<int>(Weight::Bold);
  node_label_->style.color = Color4f::hex(0x2C1F03);
  node_label_->style.ellipsis = true;
  add_child(std::move(label));

  // 徽章：wifi + 延迟数（字色随主题；胶囊底在 on_paint 绘制）
  // src: active-node-card.tsx:333-336（Chip label fontSize 11px weight 600）
  auto wifi = std::make_unique<IconW>();
  wifi_ = wifi.get();
  wifi_->set_icon("wifi");
  wifi_->size_ = 12;  // src: active-node-card.tsx:350 MuiChip-icon fontSize 12px
  wifi_->color_ = Color4f::hex(0x0075FF);
  wifi_->set_visible(false);
  add_child(std::move(wifi));

  auto dl = std::make_unique<Label>();
  delay_label_ = dl.get();
  delay_label_->style.size = 11;
  delay_label_->style.weight = 600;  // src: active-node-card.tsx:333-336 Chip label weight 600
  delay_label_->style.color = Color4f::hex(0x0075FF);
  delay_label_->set_visible(false);
  add_child(std::move(dl));

  // 节点地址 "(server:port)"（11px 粗体；≤400 隐藏）
  // src: active-node-card.tsx:356-370
  auto addr = std::make_unique<Label>();
  addr_label_ = addr.get();
  addr_label_->style.size = 11;                                 // src: :362
  addr_label_->style.weight = static_cast<int>(Weight::Bold);   // src: :361
  addr_label_->style.ellipsis = true;
  addr_label_->set_visible(false);  // 无地址时不渲染（src: :356 {nodeAddr && ...}）
  add_child(std::move(addr));

  // 图钉（置顶窗口 T19）：28×28 命中盒，3D 按钮底在 on_paint 铺
  auto pin = std::make_unique<IconW>();
  pin_ = pin.get();
  pin->set_icon("pushpin");
  pin->size_ = 20;  // src: _layout.tsx:1353-1355 fontSize/width/height 20px
  pin->color_ = Color4f::hex(0x888888);
  pin->box_w = 28;
  pin->box_h = 28;
  pin->set_tooltip_text(tr("layout.a11y.pinWindow"));  // src: locales/zh/layout.json:100 a11y.pinWindow
  pin->on_click = [this] {
    if (on_toggle_pin) on_toggle_pin();
  };
  add_child(std::move(pin));

  // 齿轮（设置抽屉 T17）
  auto gear = std::make_unique<IconW>();
  gear_ = gear.get();
  gear->set_icon("settings");
  gear->size_ = 20;  // src: _layout.tsx:1385 fontSize/width/height 20px
  gear->color_ = Color4f::hex(0x2C1F03);
  gear->box_w = 28;
  gear->box_h = 28;
  gear->set_tooltip_text(tr("layout.a11y.openSettings"));  // src: locales/zh/layout.json:101 a11y.openSettings
  gear->on_click = [this] {
    if (on_open_settings) on_open_settings();
  };
  add_child(std::move(gear));
}

QuickBar::~QuickBar() = default;

SizeF QuickBar::on_measure(float max_w, float max_h) {
  SizeF s = Row::on_measure(max_w, max_h);
  s.h = 30;  // src: _layout.tsx:1318 快捷条容器 height 30px（内含卡高 28，见 active-node-card.tsx:241）
  return s;
}

// src: active-node-card.tsx:298-302「/\\s\\(\\d{6}\\)$/」→ 展示名去掉末尾 6 位延迟后缀
static std::wstring strip_six_digit_suffix(const std::wstring& text) {
  const size_t n = text.size();
  if (n < 9 || text[n - 1] != L')' || text[n - 7] != L'(' ||
      text[n - 8] != L' ')
    return text;
  for (size_t i = n - 6; i < n - 1; ++i)
    if (text[i] < L'0' || text[i] > L'9') return text;
  return text.substr(0, n - 8);
}

void QuickBar::set_node_text(const std::wstring& text) {
  const std::wstring shown = strip_six_digit_suffix(text);
  if (node_label_->text == shown) return;
  node_label_->set_text(shown);
  if (bounds().w > 0) on_arrange(bounds());  // 名称宽度变化 → 重新居中/省略
}

void QuickBar::set_node_meta(const std::string& type, const std::wstring& addr) {
  // 协议名（src: active-node-card.tsx:278「getFriendlyProtocolName(...) || 'Direct'」）
  std::wstring proto = friendly_protocol(type);
  if (proto.empty()) proto = L"Direct";
  if (proto_label_ && proto_label_->text != proto) proto_label_->set_text(proto);
  // 地址（src: :356-369「({nodeAddr})」；空 → 不渲染）
  std::wstring shown = addr.empty() ? std::wstring() : L"(" + addr + L")";
  if (addr_label_ && addr_label_->text != shown) addr_label_->set_text(shown);
  if (bounds().w > 0) on_arrange(bounds());  // 文本/地址显隐 → 重算可用宽
}

void QuickBar::set_badge(int delay, bool has_node) {
  // src: active-node-card.tsx:311-344 activeNodeName && <Chip>；delay=-1 显示 '-'，
  //   -2 显示「测试中...」（services/delay.ts:394-400 formatDelay）
  const bool testing = delay == -2;
  const bool show = has_node && delay >= -2;
  has_badge_ = show;
  delay_ = delay;
  wifi_->set_visible(show && !testing);
  // F-D11：图标形状随延迟档位切换（src: style-helpers.tsx:25-87 getSignalIcon
  //   —— >1e5/0/timeout→WifiOff、≥800→SignalWifi1Bar、≥400→2Bar、≥250→3Bar、
  //   其余→4Bar；未测(-1)→0Bar。testing(-2) 不渲染 wifi（CircularProgress）。
  if (show && !testing) {
    const char* sig = "signalwifi4bar";
    if (delay < 0) sig = "signalwifi0bar";           // 未测试
    else if (delay > 100000 || delay == 0 ||
             delay >= kNodeDelayMaxMs) sig = "wifioff";  // 错误/超时
    else if (delay >= 800) sig = "signalwifi1bar";
    else if (delay >= 400) sig = "signalwifi2bar";
    else if (delay >= 250) sig = "signalwifi3bar";
    if (wifi_->name() != sig) wifi_->set_icon(sig);
  }
  delay_label_->set_visible(show);
  if (testing)
    delay_label_->set_text(tr("tests.unlock.page.actions.testing"));  // src: locales/zh/settings.json:549 statusTesting
  else if (show)
    delay_label_->set_text(format_delay(delay));  // -1 → '-'；与节点表同源
  if (bounds().w > 0) on_arrange(bounds());  // 徽章显隐/位数变化 → 名称可用宽重算
  invalidate();
}

void QuickBar::set_pinned(bool p) {
  pinned_ = p;
  if (pin_) {
    // src: _layout.tsx:1356-1364 图钉色 pinned #FF3B30 / 未固定 #888888；
    //   src: :1362-1366 pinned 时 rotate(45deg) + drop-shadow，transform/color/filter
    //   均有 0.2s ease 过渡（过渡插值在 on_paint 推进）
    const float target = p ? 45.0f : 0.0f;
    if (target != pin_rot_to_) {
      pin_rot_from_ = pin_rot_;  // 从当前进度续跑（连续点击不跳变）
      pin_rot_to_ = target;
      pin_anim_start_ms_ = steady_ms();
    }
    pin_->glow_ = p;
  }
  invalidate();
}

// 【P0-quickbar】卡片内容布局（照 active-node-card.tsx:233-245 的 Paper）：
//   p:'0 12px'、display flex、alignItems center、justifyContent center、gap 1.5(=12px)。
//   子项顺序：活跃节点标签(:247) → 协议胶囊(:262) → 名称(:282) → 延迟 Chip(:311)
//   → 地址(:356)；断点 580/480/400 依次隐藏标签/胶囊/地址（display:none）。
//   名称按文字宽（超宽时收缩省略），Chip 固定宽不收缩。
void QuickBar::on_arrange(const RectF& area) {
  set_bounds(area);
  chip_rect_ = {};  // 每次重排先清，避免 has_badge_ 变 false 后残留旧命中区
  const float pad_top = padding().t;
  const float content_h = area.h - padding().v();
  const float btn_w = 28.0f, btn_h = 28.0f;  // src: _layout.tsx:1345-1346 28×28
  const float btn_y = pad_top + (content_h - btn_h) / 2;
  const float right = area.w - padding().r;
  if (gear_) gear_->layout_tree({right - btn_w, btn_y, btn_w, btn_h});
  if (pin_) pin_->layout_tree({right - btn_w * 2, btn_y, btn_w, btn_h});

  // 卡片：x=快捷条左 padding，右缘 = pin 左缘（quickbar gap 0，_layout.tsx:1321）
  const float card_x = padding().l;
  const float card_r = pin_ ? pin_->bounds().x : right;
  const float inner_l = card_x + 12;  // active-node-card.tsx:236 p:'0 12px'
  const float inner_r = card_r - 12;
  const float gap = 12.0f;            // active-node-card.tsx:240 gap:1.5 → 12px

  // 断点：max-width 580/480/400 → 小于等于该宽即隐藏
  // src: active-node-card.tsx:252 / :273 / :363
  const bool show_active = area.w > 580;
  const bool show_proto = area.w > 480;
  const bool show_addr = area.w > 400 && addr_label_ && !addr_label_->text.empty();
  if (active_label_ && active_label_->visible() != show_active)
    active_label_->set_visible(show_active);
  if (proto_label_ && proto_label_->visible() != show_proto)
    proto_label_->set_visible(show_proto);
  if (addr_label_ && addr_label_->visible() != show_addr)
    addr_label_->set_visible(show_addr);

  // 固定宽元素（顺序即 Mini flex 顺序）
  const SizeF as = show_active ? active_label_->measure(1e6f, content_h) : SizeF{};
  const SizeF ps = show_proto ? proto_label_->measure(1e6f, content_h) : SizeF{};
  const float proto_pad_x = 6.4f;   // src: active-node-card.tsx:271 px: 0.8 → 8*0.8
  const float proto_pad_y = 1.6f;   // src: :272 py: 0.2 → 8*0.2
  const float proto_border = 1.0f;  // src: :268 border: '1px solid'
  const float pill_w = show_proto ? ps.w + 2 * proto_pad_x + 2 * proto_border : 0;
  const float pill_h = show_proto ? ps.h + 2 * proto_pad_y + 2 * proto_border : 0;
  const SizeF ads = show_addr ? addr_label_->measure(1e6f, content_h) : SizeF{};

  // src: active-node-card.tsx:315-320 测试中图标为 CircularProgress（10×10，占同宽图标位）
  // src: MUI Chip(size=small, filled)：root padding 0、icon marginLeft 4 /
  //   marginRight -4、label paddingLeft/Right 8 ⇒ 左 4、图标后 4、右 8
  float chip_w = 0;
  if (has_badge_ && wifi_ && delay_label_) {
    const float icon_w = delay_ == -2 ? 10.0f : wifi_->measure(1e6f, content_h).w;
    chip_w = 4.0f + icon_w + 4.0f + delay_label_->measure(1e6f, content_h).w + 8.0f;
  }

  float fixed = 0;
  if (show_active) fixed += as.w + gap;
  if (show_proto) fixed += pill_w + gap;
  if (has_badge_) fixed += gap + chip_w;
  if (show_addr) fixed += gap + ads.w;

  float avail = inner_r - inner_l - fixed;
  if (avail < 0) avail = 0;
  SizeF ns = node_label_ ? node_label_->measure(1e6f, content_h) : SizeF{};
  // src: active-node-card.tsx:282-286 maxWidth: xs 120px / sm 180px（MUI sm=600）
  const float name_cap = area.w >= 600 ? 180.0f : 120.0f;
  const float name_w = std::min({ns.w, avail, name_cap});  // 省略号由 Label 绘制
  const float group_w = fixed + name_w;
  float gx = inner_l;
  if (group_w < inner_r - inner_l) gx = inner_l + ((inner_r - inner_l) - group_w) / 2;

  float x = gx;
  if (show_active && active_label_) {
    const float hh = std::min(as.h, content_h);
    active_label_->layout_tree({x, pad_top + (content_h - hh) / 2, as.w, hh});
    x += as.w + gap;
  }
  if (show_proto && proto_label_) {
    // 胶囊外框（1px 边框 / 圆角 4）：src: :268-270
    const float py = pad_top + (content_h - pill_h) / 2;
    proto_pill_ = {x, py, pill_w, pill_h};
    proto_label_->layout_tree(
        {x + proto_border + proto_pad_x, py + proto_border + proto_pad_y, ps.w, ps.h});
    x += pill_w + gap;
  }
  const float name_h = std::min(ns.h, content_h);
  if (node_label_)
    node_label_->layout_tree({x, pad_top + (content_h - name_h) / 2, name_w, name_h});
  x += name_w;

  if (has_badge_ && wifi_ && delay_label_) {
    x += gap;
    SizeF ds = delay_label_->measure(1e6f, content_h);
    // Chip 命中区：MUI Chip(size=small) 高 20（src: :315 sx height '20px'）
    const float chip_h = 20.0f;
    chip_rect_ = {x, pad_top + (content_h - chip_h) / 2, chip_w, chip_h};
    if (delay_ == -2) {
      // 测试中：icon 位 10×10（src: :315-320 CircularProgress size 10）
      delay_label_->layout_tree({x + 4.0f + 10.0f + 4.0f,
                                 pad_top + (content_h - ds.h) / 2, ds.w, ds.h});
    } else {
      SizeF ws = wifi_->measure(1e6f, content_h);
      wifi_->layout_tree({x + 4.0f, pad_top + (content_h - ws.h) / 2, ws.w, ws.h});
      delay_label_->layout_tree({x + 4.0f + ws.w + 4.0f,
                                 pad_top + (content_h - ds.h) / 2, ds.w, ds.h});
    }
    x += chip_w;
  }

  if (show_addr && addr_label_) {
    x += gap;
    const float ah = std::min(ads.h, content_h);
    addr_label_->layout_tree(
        {x, pad_top + (content_h - ah) / 2, std::min(ads.w, inner_r - x), ah});
  }
}

bool QuickBar::on_mouse(const MouseEvent& e) {
  if (!node_label_ || node_label_->text.empty()) return false;
  // 延迟 Chip 先判：点一下测单节点延迟（src: :311-344 onClick={handleTestDelay}）
  if (has_badge_ && chip_rect_.contains(e.pos.x, e.pos.y)) {
    if (e.type == MouseEventType::Down) {
      chip_pressed_ = true;
      return true;
    }
    if (e.type == MouseEventType::Up) {
      const bool fired = chip_pressed_;
      chip_pressed_ = false;
      if (fired && on_test_delay) on_test_delay();
      return fired;
    }
    return true;  // Move：吃掉，避免与节点名 hover 互相打断
  }
  if (e.type == MouseEventType::Up) chip_pressed_ = false;
  const RectF nb = node_label_->bounds();
  const bool inside = e.pos.x >= nb.x && e.pos.x < nb.right() &&
                      e.pos.y >= nb.y && e.pos.y < nb.bottom();
  if (e.type == MouseEventType::Move) {
    if (node_hover_ != inside) {
      node_hover_ = inside;
      invalidate();
    }
    return inside;
  }
  if (e.type == MouseEventType::Down) {
    node_pressed_ = inside;
    if (inside) invalidate();
    return inside;
  }
  if (e.type == MouseEventType::Up) {
    const bool fired = node_pressed_ && inside;  // src: :284 onClick={handleCycleNode}
    node_pressed_ = false;
    invalidate();
    if (fired && on_cycle_node) on_cycle_node();
    return fired;
  }
  return false;
}

void QuickBar::on_paint(IRenderTarget& rt) {
  const float w = bounds().w, h = bounds().h;  // 局部坐标约定 {0,0,w,h}
  // 条底透明：src: _layout.tsx:1310-1322 快捷条容器无 background 声明，
  //   透出内容底色（外层 Paper :1237 background:'transparent'）；不涂任何底色。
  (void)w;
  // 图钉 0.2s ease 过渡推进（transform/color）：src: _layout.tsx:1365-1366
  if (pin_ && pin_anim_start_ms_ > 0) {
    const double e = (steady_ms() - pin_anim_start_ms_) / 200.0;
    const float t = static_cast<float>(std::min(1.0, e));
    const float k = t * t * (3.0f - 2.0f * t);  // 等价改写：smoothstep 逼近 ease 曲线
    pin_rot_ = pin_rot_from_ + (pin_rot_to_ - pin_rot_from_) * k;
    pin_->rotate_deg = pin_rot_;
    const bool to_pin = pin_rot_to_ > 0;
    const Color4f gray = Color4f::hex(0x888888), red = Color4f::hex(0xFF3B30);
    const Color4f a = to_pin ? gray : red, b = to_pin ? red : gray;
    pin_->color_ = {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k,
                    a.b + (b.b - a.b) * k, 1.0f};
    if (t >= 1.0f) {
      pin_anim_start_ms_ = 0;  // 过渡结束
      pin_rot_ = pin_rot_to_;
      pin_->rotate_deg = pin_rot_;
      pin_->color_ = b;
    } else {
      invalidate();  // 过渡期间自续帧
    }
  }
  // 选中卡：src: active-node-card.tsx:235-241（m:0 高28 p:'0 12px'）+
  //   button-styles.ts:538-539（135° 四停靠渐变）+ :549（圆角6）
  if (pin_) {
    RectF card{8, 0, pin_->bounds().x - 8, 28};  // 高 = 30-2 = 28，m:0 顶对齐
    if (card.w > 0) {
      paint_card_gradient(rt, card);
      // 底部 bevel 暗边：src: button-styles.ts:540 bevelShadowDark
      rt.fill_rect({card.x, card.bottom() - 1, card.w, 1},
                   Brush{card_bevel_dark()});
    }
    // pin/gear 的 3D 按钮底（图标在子层绘制，先铺底）
    // src: _layout.tsx:1330-1387 IconButton + get3DButtonStyle('contained', ...)
    paint_button_bg(rt, pin_->bounds(), pinned_, pin_->hover_, pin_->pressed_);
    if (gear_)
      paint_button_bg(rt, gear_->bounds(), false, gear_->hover_, gear_->pressed_);
  }
  // 卡片内容文字色（retro-3d 暗态统一 #2C1F03，其余取 MUI token）：
  //   名称 src: active-node-card.tsx:288（text.primary）
  //   标签/地址 src: :249 / :359（text.secondary）
  //   协议 src: :267（primary.main）
  const bool rd = is_retro3d_dark();
  const theme::Palette& pal = ThemeService::instance().palette();
  const Color4f rd_ink = Color4f::hex(0x2C1F03);
  // src: :296-301 hover→primary.main（retro 暗态 rgba(44,31,3,.65)）；
  //   active 仅 retro 暗态 rgba(44,31,3,.45)，亮色态保持 text.primary
  Color4f name_ink = rd ? rd_ink : pal.primary_text;
  if (node_hover_) name_ink = rd ? Color4f(44 / 255.0f, 31 / 255.0f, 3 / 255.0f, 0.65f)
                                   : pal.primary_color;
  if (node_pressed_ && rd)
    name_ink = Color4f(44 / 255.0f, 31 / 255.0f, 3 / 255.0f, 0.45f);
  const Color4f caption_ink = rd ? rd_ink : pal.secondary_text;
  const Color4f proto_ink = rd ? rd_ink : pal.primary_color;
  if (node_label_) node_label_->style.color = name_ink;
  if (active_label_) active_label_->style.color = caption_ink;
  if (addr_label_) addr_label_->style.color = caption_ink;
  if (proto_label_) proto_label_->style.color = proto_ink;
  // 齿轮在金色 3D 钮上，取卡片墨色：src: button-styles.ts:535
  if (gear_) gear_->color_ = card_ink();

  // 协议胶囊外框：1px 边框 + 圆角 4（src: active-node-card.tsx:268-270）
  if (proto_label_ && proto_label_->visible() && proto_pill_.w > 0) {
    rt.round_rect(proto_pill_, 4.0f, Pen{proto_ink, 1.0f}, nullptr);
  }
  // 延迟 Chip 五档色（与节点表格共用 formatDelayColor 口径）：
  //   src: active-node-card.tsx:331-352 color={delayColor}、bgcolor=alpha(色,0.12)
  if (has_badge_ && delay_ == -2 && delay_label_) {
    // src: active-node-card.tsx:331-344 测试中 bgcolor 缺省、字色取 delayColor(secondary)
    delay_label_->style.color = caption_ink;
    // src: active-node-card.tsx:315-320 testing 时 Chip icon 为 <CircularProgress size={10}/>
    // 等价改写：渲染器无 arc 描边接口，用 8 段渐隐圆点环绕（1.4s 周期旋转）呈现
    //   MUI 不定进度环造型；槽位宽 10px 由 on_arrange 预留（:352-354）。
    const float slot_x = delay_label_->bounds().x - 14.0f;  // 4 + 10：icon 左距 + 宽
    const float cx = slot_x + 5.0f, cy = (h - 10.0f) / 2 + 5.0f;
    const double ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    const double phase = std::fmod(ms / 1400.0, 1.0);  // MUI 不定环旋转周期
    for (int i = 0; i < 8; ++i) {
      const double ang = (phase + static_cast<double>(i) / 8.0) * 6.283185307;
      const float dot = 1.7f, rad = 4.0f;
      Color4f ink = caption_ink;
      ink.a = 0.9f * (1.0f - static_cast<float>(i) / 8.0f);  // 头亮尾暗
      Brush db{ink};
      rt.round_rect({cx + static_cast<float>(std::cos(ang)) * rad - dot / 2,
                     cy + static_cast<float>(std::sin(ang)) * rad - dot / 2, dot,
                     dot},
                    dot / 2, Pen{ink, 0.0f}, &db);
    }
    invalidate();  // 动画期间自续帧（30fps 帧泵）
  }
  if (has_badge_ && delay_ >= -1 && wifi_ && delay_label_) {
    // src: active-node-card.tsx:331-344 未测(-1) 取 text.secondary；bgcolor alpha(色,.12)
    const Color4f tier =
        delay_ >= 0 ? delay_tier_color(delay_tier(delay_))
                    : theme::ThemeService::instance().palette().secondary_text;
    wifi_->color_ = tier;
    delay_label_->style.color = tier;
    // Chip：src: active-node-card.tsx:335-336 height 20px、圆角胶囊；内边距 6
    float x0 = wifi_->bounds().x - 4, x1 = delay_label_->bounds().right() + 8;
    const float y0 = (h - 20) / 2;
    Pen none{Color4f(0, 0, 0, 0), 0};
    Brush pb{tier.r, tier.g, tier.b, 0.12f};
    rt.round_rect({x0, y0, x1 - x0, 20}, 10.0f, none, &pb);
  }
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
    CWLOG_INFO("[TAKEOVER] Down local=(" + std::to_string((int)e.pos.x) + "," +
               std::to_string((int)e.pos.y) + ") bounds=(" +
               std::to_string((int)bounds().x) + "," +
               std::to_string((int)bounds().y) + "," +
               std::to_string((int)bounds().w) + "," +
               std::to_string((int)bounds().h) + ") idx=" +
               std::to_string(pressed_) + " mode=" + mode_);
    return true;
  }
  if (e.type == MouseEventType::Up) {
    int idx = hit_segment(e.pos);
    pressed_ = -1;
    CWLOG_INFO("[TAKEOVER] Up local=(" + std::to_string((int)e.pos.x) + "," +
               std::to_string((int)e.pos.y) + ") idx=" + std::to_string(idx) +
               " mode=" + mode_ + " apply=" + (apply ? "set" : "NULL"));
    if (idx >= 0 && idx < 3) {
      std::string next = takeover_mode(idx);
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
  // src: takeover-mode-card.tsx:64-77 容器（radius 4 / p 1px / h 22）
  //   + button-styles.ts:939-945 retro-3d 容器：1px rgba(212,175,55,.35/.22)、
  //   bg rgba(212,175,55,.06/.08)
  // on_paint 局部坐标约定 {0,0,w,h}（origin 已是本件全局原点，
  // 用 bounds() 会二次偏移）
  RectF r{0, 0, bounds().w, bounds().h};
  const bool light = ThemeService::instance().is_light();
  Brush cont_bg{Color4f::hex(0xD4AF37, light ? 0.06f : 0.08f)};
  rt.round_rect(r, 4.0f,
                Pen{Color4f::hex(0xD4AF37, light ? 0.35f : 0.22f), 1.0f}, &cont_bg);
  float seg_w = r.w / 3.0f;
  for (int i = 0; i < 3; ++i) {
    RectF seg{r.x + seg_w * i, r.y, seg_w, r.h};
    bool sel = mode_ == takeover_mode(i);
    if (sel) {
      // src: button-styles.ts:999-1007 选中块 absolute inset 1px / radius 3 /
      //   135° 六停金渐变；未选中段无底色（takeover-mode-card.tsx:102-138）
      RectF pill{seg.x + 1, seg.y + 1, seg.w - 2, seg.h - 2};
      GradientStop st[6];
      st[0] = {0.00f, Color4f::hex(0xFFE082)};
      st[1] = {0.20f, Color4f::hex(0xFFD54F)};
      st[2] = {0.45f, Color4f::hex(0xFFC107)};
      st[3] = {0.70f, Color4f::hex(0xFFB300)};
      st[4] = {0.85f, Color4f::hex(0xD4AF37)};
      st[5] = {1.00f, Color4f::hex(0xB8860B)};
      rt.fill_round_rect_gradient_angle(pill, 3.0f, st, 6, 135.0f);
    }
    TextStyle ts;
    ts.size = 13;  // src: takeover-mode-card.tsx:116-117 zh 13px（非 zh 11px）
    // src: button-styles.ts:1110 选中文字 '#2C1F03'；
    //   未选中 takeover-mode-card.tsx:110-115 rgba(44,31,3,.75) / text.secondary
    //   hover :122-129 dark '#2C1F03'；active :130-137 dark rgba(44,31,3,.5)
    Color4f color = sel ? Color4f::hex(0x2C1F03)
                        : (light ? Color4f::hex(0x3C3C43, 0.6f)
                                 : Color4f::hex(0x2C1F03, 0.75f));
    if (!sel && !light) {
      if (pressed_ == i) color = Color4f::hex(0x2C1F03, 0.5f);
      else if (hover_ == i) color = Color4f::hex(0x2C1F03);
    }
    ts.color = color;
    ts.weight = static_cast<int>(Weight::Bold);  // src: :118 fontWeight 'bold'
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    ts.ellipsis = true;
    rt.draw_text(takeover_label(i), seg, ts);
  }
}

}  // namespace clashwin::ui::scene

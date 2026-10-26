// 设置抽屉实现（T17）：布局/样式逐项对照 clashwin_drawer.png。
#include "settings_drawer.h"

#include <windows.h>

#include <fstream>
#include <sstream>

#include "../theme/theme.h"
#include "i18n/i18n.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

namespace utf = clashwin::utf;
using theme::ThemeService;

// ---------- 皮肤/因子持久化（clashwin-skin.json） ----------
SkinPrefs load_skin_prefs() {
  SkinPrefs p;
  std::wstring path = util::dirs::app_home_dir() + L"\\clashwin-skin.json";
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return p;
  std::stringstream ss;
  ss << f.rdbuf();
  try {
    auto j = nlohmann::json::parse(ss.str());
    p.skin = j.value("skin", p.skin);
    p.theme_mode = j.value("theme_mode", p.theme_mode);
    p.depth = j.value("depth", p.depth);
    p.vibrancy = j.value("vibrancy", p.vibrancy);
  } catch (const std::exception&) {
    // 损坏 → 缺省
  }
  return p;
}

void save_skin_prefs(const SkinPrefs& p) {
  std::wstring path = util::dirs::app_home_dir() + L"\\clashwin-skin.json";
  nlohmann::json j;
  j["skin"] = p.skin;
  j["theme_mode"] = p.theme_mode;
  j["depth"] = p.depth;
  j["vibrancy"] = p.vibrancy;
  std::ofstream f(path, std::ios::binary);
  if (f.is_open()) f << j.dump();
}

// ---------- AccentColor（DWM；← 原版 CSS AccentColor 探测语义） ----------
std::optional<uint32_t> system_accent_color_rgb() {
  // DwmGetColorizationColor：DWORD 0xAARRGGBB；失败（未开主题色）→ nullopt
  // 语义对齐原版：取系统主题强调色作为 primary 兜底
  HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
  if (!dwm) return std::nullopt;
  using Fn = HRESULT(__stdcall*)(DWORD*, BOOL*);
  auto fn = reinterpret_cast<Fn>(GetProcAddress(dwm, "DwmGetColorizationColor"));
  std::optional<uint32_t> out;
  if (fn) {
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(fn(&color, &opaque))) out = color & 0x00FFFFFF;
  }
  FreeLibrary(dwm);
  return out;
}

// ---------- PanelCard ----------
PanelCard::PanelCard(const std::wstring& title, Widget* parent)
    : Widget(parent) {
  auto t = std::make_unique<Label>();
  title_ = t.get();
  title_->style.size = 13;
  title_->style.weight = static_cast<int>(Weight::Bold);
  title_->set_text(title);
  add_child(std::move(t));
}

void PanelCard::on_paint(IRenderTarget& rt) {
  const auto& svc = ThemeService::instance();
  // 卡片底（← 截图浅灰面板 + 圆角）；on_paint 局部坐标 {0,0,w,h}
  Brush bg{svc.vars().theme_panel_bg};
  rt.round_rect({0, 0, bounds().w, bounds().h}, 6,
                {svc.vars().theme_border, 1}, &bg);
  title_->style.color = Color4f::hex(0xE8890C);  // ← 截图橙标题
}

void PanelCard::on_arrange(const RectF& area) {
  set_bounds(area);
  // 【坐标约定】子布局局部坐标
  float y = 6;
  title_->layout_tree({10, y, area.w - 20, 20});
  arrange_content({10, y + 22, area.w - 20, content_h_});
}

// ---------- TakeoverCard ----------
TakeoverCard::TakeoverCard() : PanelCard(L"流量接管模式") {
  auto seg = std::make_unique<TakeoverSegmented>();
  seg_ = seg.get();
  seg_->layout_tree({0, 0, 300, 26});
  add_child(std::move(seg));
  auto hint = std::make_unique<Label>();
  hint_ = hint.get();
  hint_->style.size = 11;
  hint_->style.color = Color4f::hex(0x3C3C43, 0.6f);
  add_child(std::move(hint));
  set_content_height(46);
}

void TakeoverCard::arrange_content(const RectF& content) {
  seg_->layout_tree({content.x, content.y, content.w, 26});
  hint_->layout_tree({content.x, content.y + 30, content.w, 16});
}

// ---------- RoutingCard ----------
RoutingCard::RoutingCard() : PanelCard(L"分流路由偏好") {
  auto seg = std::make_unique<Segmented>();
  seg_ = seg.get();
  // ← T10 Segmented 缺省选中态：白底 + 主色字 + 粗体（截图 RUL 同款）
  seg_->set_options({L"DIR", L"RUL", L"PRX"}, 1);
  add_child(std::move(seg));
  auto hint = std::make_unique<Label>();
  hint_ = hint.get();
  hint_->style.size = 11;
  hint_->style.color = Color4f::hex(0x3C3C43, 0.6f);
  hint_->set_text(L"RUL: 智能规则分流 (推荐出厂默认)");
  add_child(std::move(hint));
  set_content_height(46);
}

void RoutingCard::arrange_content(const RectF& content) {
  seg_->layout_tree({content.x, content.y, content.w, 26});
  hint_->layout_tree({content.x, content.y + 30, content.w, 16});
}

// ---------- BasicCard ----------
BasicCard::BasicCard() : PanelCard(L"基础设置") {
  struct Def {
    const wchar_t* text;
    SwitchW** slot;
  } defs[4] = {{L"开机自启动", &sw_auto_launch_},
               {L"Allow LAN", &sw_allow_lan_},
               {L"启动最小化", &sw_silent_start_},
               {L"Allow IPv6", &sw_allow_ipv6_}};
  for (int i = 0; i < 4; ++i) {
    auto lb = std::make_unique<Label>();
    labels_[i] = lb.get();
    labels_[i]->style.size = 12;
    labels_[i]->set_text(defs[i].text);
    add_child(std::move(lb));
    auto sw = std::make_unique<SwitchW>();
    *defs[i].slot = sw.get();
    add_child(std::move(sw));
  }
  auto pf = std::make_unique<TextField>();
  port_field_ = pf.get();
  port_field_->set_placeholder(L"10801");
  add_child(std::move(pf));
  auto btn = std::make_unique<Button>(L"保存");
  save_btn_ = btn.get();
  btn->themed_ = true;
  add_child(std::move(btn));
  set_content_height(96);
}

void BasicCard::arrange_content(const RectF& content) {
  // 2×2：左列 label(x)+switch；右列同
  float half = content.w / 2;
  for (int i = 0; i < 4; ++i) {
    float cx = content.x + (i % 2) * half;
    float cy = content.y + (i / 2) * 28;
    labels_[i]->layout_tree({cx, cy + 2, half - 44, 18});
    SwitchW* sw = i == 0   ? sw_auto_launch_
                  : i == 1 ? sw_allow_lan_
                  : i == 2 ? sw_silent_start_
                           : sw_allow_ipv6_;
    sw->layout_tree({cx + half - 44, cy, 40, 22});
  }
  // Mixed Port 行
  float py = content.y + 60;
  port_field_->layout_tree({content.x + 90, py, content.w - 170, 24});
  save_btn_->layout_tree({content.right() - 70, py - 1, 60, 26});
}

// ---------- ThemeCard ----------
ThemeCard::ThemeCard() : PanelCard(L"界面外观与物质质感") {
  // 皮肤条 2×3
  const wchar_t* skins[] = {L"3D", L"Org", L"Mod", L"Fro", L"Cyber", L"Mono"};
  for (int i = 0; i < 6; ++i) {
    auto b = std::make_unique<Button>(skins[i]);
    skin_buttons_.push_back(b.get());
    add_child(std::move(b));
  }
  auto mode = std::make_unique<Segmented>();
  mode_seg_ = mode.get();
  mode_seg_->set_options({L"SYS", L"LIGHT", L"DARK"}, 0);
  add_child(std::move(mode));
  auto dl = std::make_unique<Label>();
  depth_label_ = dl.get();
  depth_label_->style.size = 12;
  depth_label_->set_text(L"金条拟物景深 (Depth)");
  add_child(std::move(dl));
  auto ds = std::make_unique<Slider>();
  depth_slider_ = ds.get();
  depth_slider_->min_ = 0;
  depth_slider_->max_ = 2;
  depth_slider_->step_ = 0.1f;
  add_child(std::move(ds));
  auto dv = std::make_unique<Label>();
  depth_value_ = dv.get();
  depth_value_->style.size = 11;
  add_child(std::move(dv));
  auto vl = std::make_unique<Label>();
  vibrancy_label_ = vl.get();
  vibrancy_label_->style.size = 12;
  vibrancy_label_->set_text(L"光泽饱和度 (Vibrancy)");
  add_child(std::move(vl));
  auto vs = std::make_unique<Slider>();
  vibrancy_slider_ = vs.get();
  vibrancy_slider_->min_ = 0;
  vibrancy_slider_->max_ = 2;
  vibrancy_slider_->step_ = 0.1f;
  add_child(std::move(vs));
  auto vv = std::make_unique<Label>();
  vibrancy_value_ = vv.get();
  vibrancy_value_->style.size = 11;
  add_child(std::move(vv));
  set_content_height(150);
}

void ThemeCard::arrange_content(const RectF& content) {
  // 皮肤条 2 行 × 3 列
  float cw = content.w / 3;
  for (int i = 0; i < 6; ++i) {
    skin_buttons_[i]->layout_tree({content.x + (i % 3) * cw + 2,
                                   content.y + (i / 3) * 30, cw - 4, 26});
  }
  float y = content.y + 62;
  mode_seg_->layout_tree({content.x, y, content.w, 26});
  y += 32;
  depth_label_->layout_tree({content.x, y, content.w - 40, 16});
  depth_slider_->layout_tree({content.x, y + 16, content.w - 60, 18});
  depth_value_->layout_tree({content.right() - 46, y + 16, 46, 16});
  y += 38;
  vibrancy_label_->layout_tree({content.x, y, content.w - 40, 16});
  vibrancy_slider_->layout_tree({content.x, y + 16, content.w - 60, 18});
  vibrancy_value_->layout_tree({content.right() - 46, y + 16, 46, 16});
}

// ---------- SettingsDrawer ----------
SettingsDrawer::SettingsDrawer(Widget* parent) : Stack(parent) {
  // 遮罩（点击关闭）
  auto mask = std::make_unique<Widget>();
  mask_ = mask.get();
  add_child(std::move(mask));

  // 面板（单列；滚动）
  auto panel = std::make_unique<Column>();
  panel_ = panel.get();
  panel_->set_padding(6, 6, 6, 6);
  add_child(std::move(panel));

  // 关闭按钮 ✕（右上橙描边方形）
  auto close = std::make_unique<IconW>();
  close_btn_ = close.get();
  close_btn_->set_icon("close");
  close_btn_->size_ = 12;
  close_btn_->on_click = [this]() { set_open(false); };
  panel_->add_child(std::move(close));

  auto profile = std::make_unique<ProfileImportCard>();
  profile_card_ = profile.get();
  profile_card_->set_title_color(Color4f::hex(0xE8890C));  // ← 抽屉橙标题
  panel_->add_child(std::move(profile));

  auto takeover = std::make_unique<TakeoverCard>();
  takeover_card_ = takeover.get();
  panel_->add_child(std::move(takeover));

  auto routing = std::make_unique<RoutingCard>();
  routing_card_ = routing.get();
  panel_->add_child(std::move(routing));

  auto basic = std::make_unique<BasicCard>();
  basic_card_ = basic.get();
  panel_->add_child(std::move(basic));

  auto theme = std::make_unique<ThemeCard>();
  theme_card_ = theme.get();
  panel_->add_child(std::move(theme));

  // 底部按钮行（日志/帮助）
  auto bottom = std::make_unique<Row>();
  bottom->spacing = 6;
  auto lb = std::make_unique<Button>(L"查看运行日志");
  logs_btn_ = lb.get();
  lb->themed_ = true;
  lb->flex_weight = 1;
  bottom->add_child(std::move(lb));
  auto hb = std::make_unique<Button>(L"帮助与关于");
  help_btn_ = hb.get();
  hb->themed_ = true;
  hb->flex_weight = 1;
  bottom->add_child(std::move(hb));
  bottom->flex_weight = 0;
  panel_->add_child(std::move(bottom));

  set_open(false);
}

SettingsDrawer::~SettingsDrawer() = default;

void SettingsDrawer::set_open(bool open) {
  if (open_ == open) return;
  open_ = open;
  set_visible(true);  // Stack 宿主常驻，遮罩/面板切换可见性
  mask_->set_visible(open);
  panel_->set_visible(open);
  if (open) {
    // 面板重排（宿主 on_arrange 在可见时才有效）
    invalidate();
  }
}

void SettingsDrawer::apply_skin_prefs() {
  SkinPrefs p = load_skin_prefs();
  auto& svc = ThemeService::instance();
  if (auto skin = theme::skin_from_name(p.skin)) svc.set_skin(*skin);
  if (p.theme_mode == "light")
    svc.set_mode(theme::ThemeMode::Light);
  else if (p.theme_mode == "dark")
    svc.set_mode(theme::ThemeMode::Dark);
  else
    svc.set_mode(theme::ThemeMode::Light);
  svc.set_factors(p.depth, p.vibrancy);
}

void SettingsDrawer::on_arrange(const RectF& area) {
  set_bounds(area);
  mask_->layout_tree({0, 0, area.w, area.h});
  // 面板：顶部留 30 标题栏高（覆盖客户区其余）
  panel_->layout_tree({0, 30, area.w, area.h - 30});
  // 关闭按钮行（右对齐 30×30）
  close_btn_->layout_tree({area.w - 34, 4, 30, 28});
  // 卡片纵向排列（局部坐标）
  float y = 36;
  profile_card_->layout_tree({6, y, area.w - 12, 92});
  y += 98;
  takeover_card_->layout_tree({6, y, area.w - 12, 92});
  y += 98;
  routing_card_->layout_tree({6, y, area.w - 12, 92});
  y += 98;
  basic_card_->layout_tree({6, y, area.w - 12, 142});
  y += 148;
  theme_card_->layout_tree({6, y, area.w - 12, 196});
  y += 202;
  // 底部按钮行贴底
  logs_btn_->layout_tree({6, area.h - 116, (area.w - 18) / 2, 30});
  help_btn_->layout_tree({12 + (area.w - 18) / 2, area.h - 116,
                          (area.w - 18) / 2, 30});
  (void)y;
}

bool SettingsDrawer::on_mouse(const MouseEvent& e) {
  // 遮罩点击关闭（open 且点击落在遮罩/面板外层——面板内由子件路由）
  if (open_ && e.type == MouseEventType::Up) {
    // 命中测试：面板矩形之外即遮罩
    RectF panel_g = panel_->global_bounds();
    if (!panel_g.contains(e.pos.x, e.pos.y)) {
      set_open(false);
      return true;
    }
  }
  return Stack::on_mouse(e);
}

void SettingsDrawer::wire_callbacks() {
  // 接管三段 → patch_verge_config(enable_system_proxy/enable_tun_mode)
  //（← layout.tsx:1038-1106；tun 的 restartCore 归 T18）
  takeover_card_->seg_->apply = [this](const std::string& mode) {
    bool sys = mode == "SYS", tun = mode == "TUN";
    if (on_patch_verge) {
      on_patch_verge("enable_system_proxy", sys);
      on_patch_verge("enable_tun_mode", tun);
    }
    takeover_card_->hint_->set_text(
        mode == "SYS"   ? L"SYS: 已接管系统代理"
        : mode == "TUN" ? L"TUN: 已接管 TUN 模式"
                        : L"MAN: 手动接管（未启用系统代理/TUN）");
    return true;
  };
  // 分流三段 → patch_verge_config(rule_fallback)（DIR/RUL/PRX → direct/
  // rule/proxy；← layout.tsx:402-423 rule_fallback 语义）
  routing_card_->seg_->on_change = [this](int i) {
    static const char* vals[] = {"direct", "rule", "proxy"};
    static const wchar_t* hints[] = {
        L"DIR: 全部直连", L"RUL: 智能规则分流 (推荐出厂默认)",
        L"PRX: 全部代理"};
    if (on_patch_verge) on_patch_verge("rule_fallback", vals[i]);
    if (on_patch_clash_mode) on_patch_clash_mode("rule");
    routing_card_->hint_->set_text(hints[i]);
  };
  // 2×2 开关
  basic_card_->sw_auto_launch_->on_change = [this](bool v) {
    if (on_patch_verge) on_patch_verge("enable_auto_launch", v);
  };
  basic_card_->sw_silent_start_->on_change = [this](bool v) {
    if (on_patch_verge) on_patch_verge("enable_silent_start", v);
  };
  basic_card_->sw_allow_lan_->on_change = [this](bool v) {
    if (on_patch_clash_mode) { /* allow-lan 走 on_patch_clash_config 钩子 */ }
    if (on_patch_verge) on_patch_verge("clash_allow_lan", v);
  };
  basic_card_->sw_allow_ipv6_->on_change = [this](bool v) {
    if (on_patch_verge) on_patch_verge("clash_ipv6", v);
  };
  // Mixed Port 保存
  basic_card_->save_btn_->on_click = [this]() {
    std::wstring t = basic_card_->port_field_->text();
    if (t.empty()) return;
    int port = _wtoi(t.c_str());
    if (port < 1024 || port > 65535) {
      // 校验失败通知
      return;
    }
    if (on_patch_verge) on_patch_verge("verge_mixed_port", port);
  };
  // 皮肤条：set_skin + 持久化 + primary_override（original 强制主色外的兜底）
  static const char* skin_names[] = {"retro-3d", "original", "modern-flat",
                                     "frosted-glass", "cyberpunk",
                                     "monochrome"};
  for (size_t i = 0; i < theme_card_->skin_buttons_.size(); ++i) {
    theme_card_->skin_buttons_[i]->on_click = [this, i]() {
      auto& svc = ThemeService::instance();
      if (auto skin = theme::skin_from_name(skin_names[i])) {
        svc.set_skin(*skin);
        SkinPrefs p = load_skin_prefs();
        p.skin = skin_names[i];
        save_skin_prefs(p);
        if (on_patch_verge)
          on_patch_verge("clash_mini_control_skin", skin_names[i]);
      }
    };
  }
  // 亮暗三段 → ThemeService.set_mode + 持久化
  theme_card_->mode_seg_->on_change = [this](int i) {
    auto& svc = ThemeService::instance();
    svc.set_mode(i == 1 ? theme::ThemeMode::Light
                        : (i == 2 ? theme::ThemeMode::Dark : theme::ThemeMode::Light));
    SkinPrefs p = load_skin_prefs();
    p.theme_mode = i == 1 ? "light" : (i == 2 ? "dark" : "sys");
    save_skin_prefs(p);
    if (on_patch_verge)
      on_patch_verge("theme_mode", i == 1 ? "light" : (i == 2 ? "dark" : "system"));
  };
  // Depth/Vibrancy 滑块 → set_factors + 持久化 + 数值标签
  theme_card_->depth_slider_->on_change = [this](float v) {
    auto& svc = ThemeService::instance();
    svc.set_factors(v, svc.vibrancy_factor());
    theme_card_->depth_value_->set_text(fmt_factor(v));
    SkinPrefs p = load_skin_prefs();
    p.depth = v;
    save_skin_prefs(p);
  };
  theme_card_->vibrancy_slider_->on_change = [this](float v) {
    auto& svc = ThemeService::instance();
    svc.set_factors(svc.depth_factor(), v);
    theme_card_->vibrancy_value_->set_text(fmt_factor(v));
    SkinPrefs p = load_skin_prefs();
    p.vibrancy = v;
    save_skin_prefs(p);
  };
}

std::wstring SettingsDrawer::fmt_factor(float v) {
  wchar_t buf[16];
  swprintf(buf, 16, L"%.1f", v);
  return buf;
}

void SettingsDrawer::on_paint(IRenderTarget& rt) {
  if (open_) {
    // 遮罩（半透明黑）；on_paint 局部坐标 {0,0,w,h}
    rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{Color4f(0, 0, 0, 0.45f)});
    // 面板底（← 截图浅灰）
    const auto& svc = ThemeService::instance();
    rt.fill_rect({0, 30, bounds().w, bounds().h - 30},
                 Brush{svc.vars().theme_panel_bg});
  }
}

}  // namespace clashwin::ui::scene

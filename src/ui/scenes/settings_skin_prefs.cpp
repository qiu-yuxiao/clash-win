// 皮肤/因子持久化（clash-win-skin.json）、系统 AccentColor 与 HelpDialog 实现
// （拆分自 src/ui/scenes/settings_drawer.cpp）
#include "settings_drawer.h"
#include "settings_drawer_internal.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <sstream>

#include "../button_draw.h"
#include "../card_draw.h"
#include "../soft_shadow.h"
#include "../theme/theme.h"
#include "ui/i18n_text.h"
#include "util/dirs.h"

namespace clashwin::ui::scene {

using theme::ThemeService;

// ---------- 皮肤/因子持久化（clashwin-skin.json） ----------
// 因子按皮肤分别存储（← src/pages/_layout/hooks/use-skin-controls.ts:13-24 readFactor）
SkinPrefs load_skin_prefs() {
  SkinPrefs p;
  std::wstring path = util::dirs::app_home_dir() + L"\\clash-win-skin.json";
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) {
    std::wstring legacy = util::dirs::app_home_dir() + L"\\clashwin-skin.json";
    f.open(legacy, std::ios::binary);
  }
  if (!f.is_open()) return p;
  std::stringstream ss;
  ss << f.rdbuf();
  try {
    auto j = nlohmann::json::parse(ss.str());
    p.skin = j.value("skin", p.skin);
    p.theme_mode = j.value("theme_mode", p.theme_mode);
    p.depth = j.value("depth", p.depth);
    p.vibrancy = j.value("vibrancy", p.vibrancy);
    if (j.contains("factors") && j["factors"].is_object()) {
      for (const auto& [k, v] : j["factors"].items()) {
        if (!v.is_object()) continue;
        p.factors[k] = {v.value("v1", 1.0f), v.value("v2", 1.0f)};
      }
    }
  } catch (const std::exception&) {
    // 损坏 → 缺省
  }
  return p;
}

namespace {
std::atomic<uint64_t> g_skin_prefs_save_count{0};
}  // namespace

void save_skin_prefs(const SkinPrefs& p) {
  std::wstring path = util::dirs::app_home_dir() + L"\\clash-win-skin.json";
  nlohmann::json j;
  j["skin"] = p.skin;
  j["theme_mode"] = p.theme_mode;
  j["depth"] = p.depth;
  j["vibrancy"] = p.vibrancy;
  nlohmann::json fj = nlohmann::json::object();
  for (const auto& [k, v] : p.factors) {
    fj[k] = {{"v1", v.first}, {"v2", v.second}};
  }
  j["factors"] = std::move(fj);
  std::ofstream f(path, std::ios::binary);
  if (f.is_open()) f << j.dump();
  g_skin_prefs_save_count.fetch_add(1, std::memory_order_relaxed);
}

uint64_t skin_prefs_save_count() {
  return g_skin_prefs_save_count.load(std::memory_order_relaxed);
}

void reset_skin_prefs_save_count() {
  g_skin_prefs_save_count.store(0, std::memory_order_relaxed);
}

std::pair<float, float> skin_factors(const SkinPrefs& p,
                                     const std::string& skin) {
  auto it = p.factors.find(skin);
  if (it != p.factors.end()) return it->second;
  if (skin == "retro-3d") return {p.depth, p.vibrancy};
  float d = 1.0f, v = 1.0f;
  if (auto sid = theme::skin_from_name(skin))
    theme::skin_default_factors(*sid, &d, &v);
  else
    theme::skin_default_factors(theme::SkinId::Retro3d, &d, &v);
  return {d, v};
}

void set_skin_factors(SkinPrefs& p, const std::string& skin, float depth,
                      float vibrancy) {
  p.factors[skin] = {depth, vibrancy};
  if (skin == "retro-3d") {
    p.depth = depth;
    p.vibrancy = vibrancy;
  }
}

// ---------- AccentColor（DWM） ----------
std::optional<uint32_t> system_accent_color_rgb() {
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

// ---------- HelpDialog ----------
// src: src/pages/_layout/components/help-menu-button.tsx:125-273
namespace {
constexpr float kHelpTitlePadX = 14.0f;           // src: src/utils/popup-styles.ts:198
constexpr float kHelpTitlePadY = 10.8f;           // src: src/utils/popup-styles.ts:199
constexpr float kHelpTitleTextH = 15.5f * 1.35f;  // src: src/pages/_layout/components/help-menu-button.tsx:140-141
constexpr float kHelpContentPadX = 10.0f;         // src: src/pages/_layout/components/help-menu-button.tsx:149 px 1.25
constexpr float kHelpContentPadY = 8.0f;          // src: src/pages/_layout/components/help-menu-button.tsx:149 py 1
constexpr float kHelpItemMinH = 32.0f;            // src: src/utils/popup-styles.ts:280
constexpr float kHelpItemPadX = 10.0f;            // src: src/pages/_layout/components/help-menu-button.tsx:76 px 1.25
constexpr float kHelpItemPadY = 6.4f;             // src: src/pages/_layout/components/help-menu-button.tsx:77 py 0.8
constexpr float kHelpSepH = 9.0f;                 // src: src/pages/_layout/components/help-menu-button.tsx:200 my 0.5 + 1px divider
constexpr float kHelpActionsPadY = 10.8f;         // src: src/utils/popup-styles.ts:240
}  // namespace

HelpDialog::HelpDialog(Widget* parent) : Widget(parent) {
  set_z_order(10000);
}

void HelpDialog::open(const std::vector<MenuItem>& items,
                      std::function<void(int)> on_sel) {
  items_ = items;
  on_select_ = std::move(on_sel);
  hover_idx_ = -1;
  hover_close_ = false;
  pressed_close_ = false;
  open_ = true;
  fade_.show();
  if (bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }
  invalidate();
}

void HelpDialog::close() {
  if (!open_) return;
  open_ = false;
  hover_idx_ = -1;
  hover_close_ = false;
  pressed_close_ = false;
  fade_.hide();
  invalidate();
}

// src: src/pages/_layout/components/help-menu-button.tsx:125-273
void HelpDialog::on_arrange(const RectF& area) {
  set_bounds(area);
  item_rects_.assign(items_.size(), RectF{});
  div_ys_.clear();

  // src: src/utils/popup-styles.ts:120-185 窄窗左侧 271px
  const float cw = std::min(theme::kPopupPaneWidth, area.w);
  const float cx = 0.0f;
  const float title_h = kHelpTitlePadY * 2.0f + kHelpTitleTextH + 1.0f;
  const float list_w = std::max(40.0f, cw - kHelpContentPadX * 2.0f);
  const float text_w = std::max(20.0f, list_w - kHelpItemPadX * 2.0f);

  float list_h = 0.0f;
  std::vector<float> item_heights(items_.size(), 0.0f);
  for (size_t i = 0; i < items_.size(); ++i) {
    if (items_[i].separator) {
      item_heights[i] = kHelpSepH;
    } else {
      float ih = kHelpItemMinH;
      if (text_engine()) {
        TextStyle ts;
        // src: src/utils/popup-styles.ts:279 fontSize 12.5px
        ts.size = 12.5f;
        ts.weight = (i == 0) ? 700 : 500;
        ts.wrap = true;
        SizeF sz = text_engine()->measure_text(items_[i].label, ts, text_w);
        ih = std::max(ih, sz.h + kHelpItemPadY * 2.0f);
      }
      item_heights[i] = ih;
    }
    list_h += item_heights[i];
  }

  // src: src/pages/_layout/components/help-menu-button.tsx:261-267
  const float btn_bw =
      ThemeService::instance().button_paint("default", true).border_w;
  const float close_h = 12.5f * 1.75f + 5.2f * 2.0f + btn_bw * 2.0f;
  const float actions_h = 1.0f + kHelpActionsPadY * 2.0f + close_h;
  const float content_need = kHelpContentPadY * 2.0f + list_h;
  const float need = title_h + content_need + actions_h;
  const float ch = std::min(need, std::max(120.0f, area.h - 8.0f));
  const float cy = std::max(4.0f, (area.h - ch) * 0.5f);

  card_rect_ = {cx, cy, cw, ch};
  title_rect_ = {cx, cy, cw, title_h};
  const float inner_content_h = std::max(0.0f, ch - title_h - actions_h);
  content_rect_ = {cx, cy + title_h, cw, inner_content_h};
  btn_close_rect_ = {cx + kHelpTitlePadX,
                     cy + ch - kHelpActionsPadY - close_h,
                     std::max(40.0f, cw - kHelpTitlePadX * 2.0f), close_h};

  float iy = content_rect_.y + kHelpContentPadY;
  for (size_t i = 0; i < items_.size(); ++i) {
    if (items_[i].separator) {
      div_ys_.push_back(iy + (kHelpSepH - 1.0f) * 0.5f);
    } else {
      item_rects_[i] = {cx + kHelpContentPadX, iy, list_w, item_heights[i]};
    }
    iy += item_heights[i];
  }
}

// src: src/pages/_layout/components/help-menu-button.tsx:125-273
void HelpDialog::on_paint(IRenderTarget& rt) {
  if (fade_.advance()) invalidate();
  set_opacity(fade_.value());
  if (fade_.hidden()) return;

  const auto& svc = ThemeService::instance();
  const auto tokens = svc.popup_tokens();

  // src: src/utils/popup-styles.ts:145-157
  const float pane_w = std::min(theme::kPopupPaneWidth, bounds().w);
  rt.fill_rect({0.0f, 0.0f, pane_w, bounds().h}, Brush{Color4f(0, 0, 0, 0.5f)});

  draw_card(rt, card_rect_, svc.popup_card_paint());

  // src: src/pages/_layout/components/help-menu-button.tsx:132-147
  TextStyle title_ts;
  title_ts.size = 15.5f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = tokens.primary_text;
  title_ts.align_h = AlignH::Center;
  title_ts.align_v = AlignV::Center;
  rt.draw_text(
      L"❓ " + tr("layout.components.helpMenu.buttonAriaLabel"),
      {card_rect_.x + kHelpTitlePadX, card_rect_.y + kHelpTitlePadY,
       std::max(20.0f, card_rect_.w - kHelpTitlePadX * 2.0f), kHelpTitleTextH},
      title_ts);
  rt.fill_rect({card_rect_.x, title_rect_.bottom() - 1.0f, card_rect_.w, 1.0f},
               Brush{tokens.divider_color});

  MenuStyle mst;
  apply_menu_skin(mst);
  const float hover_r = (svc.skin() == theme::SkinId::Cyberpunk ||
                         svc.skin() == theme::SkinId::Monochrome)
                            ? 0.0f
                            : 6.0f;  // src: src/pages/_layout/components/help-menu-button.tsx:75
  rt.push_clip(content_rect_);
  for (float dy : div_ys_) {
    rt.fill_rect({card_rect_.x + kHelpContentPadX, dy,
                  std::max(0.0f, card_rect_.w - kHelpContentPadX * 2.0f), 1.0f},
                 Brush{tokens.divider_color});
  }
  for (size_t i = 0; i < items_.size(); ++i) {
    if (items_[i].separator) continue;
    const RectF& ir = item_rects_[i];
    const bool hot = (static_cast<int>(i) == hover_idx_) && !items_[i].disabled;
    if (hot) {
      if (mst.hover_glow) {
        draw_glow(rt, ir, hover_r, mst.hover_glow_color, mst.hover_glow_blur);
      }
      Brush hb{mst.hover_bg};
      rt.round_rect(ir, hover_r, Pen{Color4f{0, 0, 0, 0}, 0.0f}, &hb);
      if (mst.hover_border_w > 0.01f && mst.hover_border.a > 0.001f) {
        rt.round_rect(ir, hover_r, Pen{mst.hover_border, mst.hover_border_w},
                      nullptr);
      }
    }
    TextStyle its;
    its.size = 12.5f;  // src: src/utils/popup-styles.ts:279
    its.weight = (i == 0) ? 700 : mst.font_weight;  // src: src/pages/_layout/components/help-menu-button.tsx:83
    its.wrap = true;
    its.align_v = AlignV::Center;
    if (items_[i].disabled) {
      its.color = Color4f(mst.disabled_text.r, mst.disabled_text.g,
                          mst.disabled_text.b, mst.disabled_text.a * 0.55f);
    } else if (hot && mst.hover_text.a > 0.001f) {
      its.color = mst.hover_text;
    } else {
      its.color = mst.text;
    }
    rt.draw_text(items_[i].label,
                 {ir.x + kHelpItemPadX, ir.y + kHelpItemPadY,
                  std::max(10.0f, ir.w - kHelpItemPadX * 2.0f),
                  std::max(10.0f, ir.h - kHelpItemPadY * 2.0f)},
                 its);
  }
  rt.pop_clip();

  rt.fill_rect({card_rect_.x, content_rect_.bottom(), card_rect_.w, 1.0f},
               Brush{tokens.divider_color});
  const auto bp = svc.button_paint("default", true);
  draw_button_skin(rt, btn_close_rect_, bp, hover_close_ ? 1.0f : 0.0f,
                   pressed_close_);
  TextStyle bts;
  // src: src/pages/_layout/components/help-menu-button.tsx:266 fontSize 12.5px
  bts.size = 12.5f;
  bts.weight = bp.weight;
  bts.color = bp.text;
  bts.align_h = AlignH::Center;
  bts.align_v = AlignV::Center;
  rt.draw_text(tr("shared.actions.close"), btn_close_rect_, bts);
}

bool HelpDialog::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  const float pane_w = std::min(theme::kPopupPaneWidth, bounds().w);
  if (e.pos.x > pane_w) {
    if (hover_idx_ != -1 || hover_close_ || pressed_close_) {
      hover_idx_ = -1;
      hover_close_ = false;
      pressed_close_ = false;
      invalidate();
    }
    return false;
  }

  int hit_idx = -1;
  if (content_rect_.contains(e.pos)) {
    for (size_t i = 0; i < items_.size(); ++i) {
      if (!items_[i].separator && !items_[i].disabled &&
          item_rects_[i].contains(e.pos)) {
        hit_idx = static_cast<int>(i);
        break;
      }
    }
  }
  const bool hit_close = btn_close_rect_.contains(e.pos);

  if (e.type == MouseEventType::Move || e.type == MouseEventType::Enter) {
    if (hit_idx != hover_idx_ || hit_close != hover_close_) {
      hover_idx_ = hit_idx;
      hover_close_ = hit_close;
      invalidate();
    }
    return true;
  }
  if (e.type == MouseEventType::Leave) {
    hover_idx_ = -1;
    hover_close_ = false;
    pressed_close_ = false;
    invalidate();
    return true;
  }
  if (e.type == MouseEventType::Down && (e.buttons & kMouseLeft)) {
    if (hit_close) {
      pressed_close_ = true;
      invalidate();
    }
    return true;
  }
  if (e.type == MouseEventType::Up &&
      (e.buttons == 0 || (e.buttons & kMouseLeft))) {
    const bool was_pressed = pressed_close_;
    pressed_close_ = false;
    if (hit_close && was_pressed) {
      close();
      if (on_dismiss) on_dismiss();
      return true;
    }
    if (hit_idx >= 0) {
      auto cb = on_select_;
      close();
      if (cb) cb(hit_idx);
      return true;
    }
    if (!card_rect_.contains(e.pos)) {
      close();
      if (on_dismiss) on_dismiss();
      return true;
    }
    invalidate();
    return true;
  }
  return true;
}

bool HelpDialog::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down && e.key == VK_ESCAPE) {
    close();
    if (on_dismiss) on_dismiss();
    return true;
  }
  return false;
}

std::wstring HelpDialog::a11y_default_name() const {
  return tr("layout.components.helpMenu.buttonAriaLabel");
}

}  // namespace clashwin::ui::scene

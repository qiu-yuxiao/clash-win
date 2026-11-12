// 订阅导入卡实现（T15 / T27：订阅列表 + 行内更新/删除 + 右键菜单入口）。
// 数值/颜色出处逐条对应 pages/_layout/components/profile-import-card.tsx。
#include "profile_import_card.h"

#include <algorithm>
#include <cmath>
#include <ctime>

#include "../card_draw.h"
#include "../icon_w.h"
#include "../theme/theme.h"
#include "util/log.h"
#include "util/parse_traffic.h"
#include "util/utf.h"
#include "ui/i18n_text.h"  // F-D01

namespace clashwin::ui::scene {

namespace utf = clashwin::utf;
using theme::ThemeService;
namespace config = clashwin::config;

namespace {

std::string fmt_bytes(uint64_t bytes) {
  auto [val, unit] = util::parse_traffic(static_cast<double>(bytes));
  return val + unit;
}

std::string fmt_date(uint64_t unix_secs) {
  if (unix_secs == 0) return "-";
  time_t t = static_cast<time_t>(unix_secs);
  struct tm lt {};
  if (localtime_s(&lt, &t) != 0) return "-";
  char buf[32];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", lt.tm_year + 1900,
                lt.tm_mon + 1, lt.tm_mday);
  return buf;
}

// MUI action.hover 缺省值（src: MUI theme palette action.hover）
Color4f action_hover(bool light) {
  return light ? Color4f::hex(0x000000, 0.04f)  // light rgba(0,0,0,0.04)
               : Color4f::hex(0xFFFFFF, 0.08f); // dark  rgba(255,255,255,0.08)
}

}  // namespace

// ---------------- ProfileRowWidget ----------------
// src: profile-import-card.tsx:217-366 行容器
void ProfileRowWidget::bind(const config::PrfItem& item, bool active) {
  uid_ = item.uid.value_or("");
  name_ = item.name.value_or("");
  type_ = item.itype.value_or("");
  active_ = active;
  // src: :205 isHighlighted = isActive || uid === 'L_Direct_Imports'
  highlighted_ = active || uid_ == "L_Direct_Imports";
  has_extra_ = item.extra.has_value();
  const auto t = item.type();
  has_line2_ = (t == config::PrfType::Remote && has_extra_) ||
               t == config::PrfType::Local;
  used_.clear();
  date_.clear();
  progress_ = 0;
  has_total_ = false;
  if (item.extra) {
    const auto& e = *item.extra;
    used_ = fmt_bytes(e.download + e.upload) + " / " + fmt_bytes(e.total);
    date_ = fmt_date(e.expire);
    // src: :209-215 Math.round(((download+upload)*100)/(total+0.01))，上限 100
    progress_ = e.total > 0
                    ? static_cast<int>(std::min(
                          100.0,
                          std::round((e.download + e.upload) * 100.0 /
                                     (static_cast<double>(e.total) + 0.01))))
                    : 0;
    has_total_ = e.total > 0;
  } else if (t == config::PrfType::Local) {
    // src: profile-import-card.tsx:330-334 L_Direct_Imports → 「节点数: N」，其余 → 「本地文件」
    if (item.uid.value_or("") == "L_Direct_Imports") {
      const std::string desc = item.desc.value_or("");
      used_ = "节点数: " +
              ((desc.empty() || desc == "本地手动导入的代理节点") ? "0" : desc);
    } else {
      used_ = "本地文件";
    }
    date_ = fmt_date(static_cast<uint64_t>(item.updated.value_or(0)));
  }
  invalidate();
}

void ProfileRowWidget::on_paint(IRenderTarget& rt) {
  const bool light = ThemeService::instance().is_light();
  const auto& pal = ThemeService::instance().palette();
  // 行可视高 = 步进 - 行 mb4 - 列表 gap4（:225-226, :200）
  const float h = std::min(bounds().h, kProfileRowVisualH);
  RectF r{0, 0, bounds().w, h};
  // bgcolor light #ffffff / dark #282A36（src: :229-230）；整行 hover（src: :235-237）
  Brush bg{hover_row_ || hover_update_ || hover_delete_
               ? action_hover(light)
               : (light ? Color4f::hex(0xFFFFFF) : Color4f::hex(0x282A36))};
  // src: :233 boxShadow '0 1px 3px rgba(0,0,0,0.05)'（引擎无阴影 → 下移 1px 偏移层）
  Brush row_shadow{Color4f::hex(0x000000, 0.05f)};
  rt.round_rect({r.x, r.y + 1.0f, r.w, r.h}, 6.0f, Pen{Color4f(0, 0, 0, 0), 0},
                &row_shadow);
  rt.round_rect(r, 6.0f, Pen{Color4f(0, 0, 0, 0), 0}, &bg);  // radius 6 :227
  // borderLeft 3px solid primary.main（激活）/transparent（:231-232）
  if (highlighted_) {
    rt.fill_rect({0.0f, 0.0f, 3.0f, h}, Brush{ThemeService::instance().primary()});
  }

  // 行内 p: '6px 8px'（:225）+ 左边框 3
  const float x0 = 3.0f + 8.0f;
  const float w = bounds().w;

  // 动作图标：删除恒有（error.main），更新仅 remote（:274-292）
  // src: profile-import-card.tsx:275-292 IconButton sx={{ p: 0.1 }}
  const float del_cx = w - 8.0f - 9.0f;
  const float upd_cx = del_cx - 20.0f;
  const float btn_cy = 15.0f;

  if (type_ == "remote") {
    RectF upd_box{upd_cx - 9.0f, btn_cy - 9.0f, 18.0f, 18.0f};
    if (hover_update_) {
      Brush hbg{light ? Color4f::hex(0x000000, 0.06f) : Color4f::hex(0xFFFFFF, 0.08f)};
      rt.round_rect(upd_box, 9.0f, Pen{Color4f(0, 0, 0, 0), 0}, &hbg);
    }
    Color4f upd_col = active_ ? ThemeService::instance().primary() : pal.secondary_text;
    IconW::paint_at(rt, "refresh", {upd_cx - 6.0f, btn_cy - 6.0f, 12.0f, 12.0f}, upd_col);
  }

  RectF del_box{del_cx - 9.0f, btn_cy - 9.0f, 18.0f, 18.0f};
  if (hover_delete_) {
    Brush hbg{light ? Color4f::hex(0x000000, 0.06f) : Color4f::hex(0xFFFFFF, 0.08f)};
    rt.round_rect(del_box, 9.0f, Pen{Color4f(0, 0, 0, 0), 0}, &hbg);
  }
  IconW::paint_at(rt, "delete", {del_cx - 6.0f, btn_cy - 6.0f, 12.0f, 12.0f}, pal.error_color);

  // 名称：13px、高亮 600 / 否则 400、primary.main / text.primary、maxWidth 70%（src: :249-259）
  TextStyle name_ts;
  name_ts.size = 13;
  name_ts.weight = highlighted_ ? 600 : 400;
  name_ts.color = highlighted_ ? ThemeService::instance().primary()
                               : pal.primary_text;
  name_ts.align_v = AlignV::Center;
  name_ts.ellipsis = true;
  const float max_act_left = (type_ == "remote" ? upd_cx - 9.0f : del_cx - 9.0f);
  const float name_w = std::min(w * 0.70f, max_act_left - 4.0f - x0);
  rt.draw_text(utf::utf8_to_utf16(name_.empty() ? "未命名配置" : name_),
               {x0, 6.0f, name_w, 18.0f}, name_ts);

  if (has_line2_) {
    // 第二行 11px text.secondary、mt 0.25 = 2px（:297-342）
    TextStyle sub;
    sub.size = 11;
    sub.color = pal.secondary_text;
    sub.align_v = AlignV::Center;
    rt.draw_text(utf::utf8_to_utf16(used_), {x0, 26.0f, w * 0.62f, 15.0f}, sub);
    TextStyle right = sub;
    right.align_h = AlignH::Right;
    rt.draw_text(utf::utf8_to_utf16(date_), {w - 8.0f - w * 0.34f, 26.0f,
                                            w * 0.34f, 15.0f}, right);
    // 进度条：h2、action.hover 底、radius1、mt 0.5=4、primary.main（src: :345-363）
    if (type_ == "remote" && has_total_) {
      const float bar_y = 45.0f;
      const float bar_w = w - x0 - 8.0f;
      Brush track{action_hover(light)};
      Brush fill{ThemeService::instance().primary()};
      rt.round_rect({x0, bar_y, bar_w, 2.0f}, 1.0f,
                    Pen{Color4f(0, 0, 0, 0), 0},  // 豁免: 无边框哨兵
                    &track);
      rt.fill_rect({x0, bar_y, bar_w * progress_ / 100.0f, 2.0f}, fill);
    }
  }
}

HCURSOR ProfileRowWidget::cursor(const PointF& local) const {
  (void)local;
  return LoadCursorW(nullptr, IDC_HAND);
}

bool ProfileRowWidget::on_mouse(const MouseEvent& e) {
  const float w = bounds().w;
  const float del_cx = w - 8.0f - 9.0f;
  const float upd_cx = del_cx - 20.0f;
  const float btn_cy = 15.0f;
  const bool over_delete = (e.pos.x >= del_cx - 9.0f && e.pos.x <= del_cx + 9.0f &&
                            e.pos.y >= btn_cy - 9.0f && e.pos.y <= btn_cy + 9.0f);
  const bool over_update = (type_ == "remote" &&
                            e.pos.x >= upd_cx - 9.0f && e.pos.x <= upd_cx + 9.0f &&
                            e.pos.y >= btn_cy - 9.0f && e.pos.y <= btn_cy + 9.0f);
  if (e.type == MouseEventType::Move || e.type == MouseEventType::Enter) {
    bool changed = false;
    if (hover_row_ != true) {
      hover_row_ = true;  // src: :235-237 &:hover 整行底色
      changed = true;
    }
    if (hover_update_ != over_update) {
      hover_update_ = over_update;
      changed = true;
    }
    if (hover_delete_ != over_delete) {
      hover_delete_ = over_delete;
      changed = true;
    }
    if (changed) invalidate();
    return true;
  }
  if (e.type == MouseEventType::Down) {
    if (e.buttons & kMouseRight) {
      if (on_context) on_context(to_global(e.pos));
      return true;
    }
    if (e.buttons & kMouseLeft) {
      if (over_delete) {
        if (on_delete) on_delete();
      } else if (over_update) {
        if (on_update) on_update();
      } else if (on_select) {
        on_select();
      }
      return true;
    }
  }
  if (e.type == MouseEventType::Leave) {
    hover_row_ = hover_update_ = hover_delete_ = false;
    invalidate();
  }
  return false;
}

std::wstring ProfileRowWidget::tooltip_text() const {
  // src: profile-import-card.tsx:260,280,290 动作钮提示优先，其余区域显示完整订阅名
  if (hover_delete_) return tr("shared.actions.delete");
  if (hover_update_) return tr("shared.actions.update");
  if (hover_row_) return clashwin::utf::utf8_to_utf16(name_);
  return {};
}

std::optional<RectF> ProfileRowWidget::tooltip_anchor_rect() const {
  const RectF g = global_bounds();
  const float del_cx = g.w - 8.0f - 9.0f;
  const float upd_cx = del_cx - 20.0f;
  const float btn_cy = 15.0f;
  if (hover_delete_) {
    return RectF{g.x + del_cx - 9.0f, g.y + btn_cy - 9.0f, 18.0f, 18.0f};
  }
  if (hover_update_) {
    return RectF{g.x + upd_cx - 9.0f, g.y + btn_cy - 9.0f, 18.0f, 18.0f};
  }
  return std::nullopt;
}

std::wstring ProfileRowWidget::a11y_default_name() const {
  std::wstring out = utf::utf8_to_utf16(name_.empty() ? "未命名配置" : name_);
  if (has_line2_) {
    if (!used_.empty()) out += L" " + utf::utf8_to_utf16(used_);
    if (!date_.empty()) out += L" " + utf::utf8_to_utf16(date_);
  }
  return out;
}

size_t ProfileRowWidget::a11y_virtual_child_count() const {
  return type_ == "remote" ? 2u : 1u;
}

A11ySubItem ProfileRowWidget::a11y_virtual_child(size_t idx) const {
  A11ySubItem sub;
  sub.role = A11yRole::Button;
  sub.can_invoke = enabled();
  const float w = bounds().w;
  const float del_cx = w - 8.0f - 9.0f;  // src: src/pages/_layout/components/profile-import-card.tsx:275
  const float upd_cx = del_cx - 20.0f;   // src: src/pages/_layout/components/profile-import-card.tsx:275
  const float btn_cy = 15.0f;            // src: src/pages/_layout/components/profile-import-card.tsx:275
  if (type_ == "remote" && idx == 0) {
    sub.name = tr("shared.actions.update");
    sub.local_bounds = {upd_cx - 9.0f, btn_cy - 9.0f, 18.0f, 18.0f};
  } else {
    sub.name = tr("shared.actions.delete");
    sub.local_bounds = {del_cx - 9.0f, btn_cy - 9.0f, 18.0f, 18.0f};
  }
  return sub;
}

bool ProfileRowWidget::a11y_invoke_virtual_child(size_t idx) {
  if (!enabled()) return false;
  if (type_ == "remote" && idx == 0) {
    if (!on_update) return false;
    on_update();
    return true;
  }
  if (!on_delete) return false;
  on_delete();
  return true;
}

// ---------------- ProfileImportCard ----------------
// src: profile-import-card.tsx:94-369 卡容器 p1 + get3DCardStyle('default')
ProfileImportCard::ProfileImportCard(Widget* parent) : Widget(parent) {
  // 标题：variant subtitle2 / 13px bold / mb 0.75=6（src: :113-117）
  auto title = std::make_unique<Label>();
  title_ = title.get();
  title_->style.size = 13;
  title_->style.weight = 700;  // src: :116 fontWeight 'bold'
  title_->style.align_v = AlignV::Center;
  title_->set_i18n_text("settings.mini.profilesTitle");
  title_->flex_weight = 0;
  add_child(std::move(title));

  // URL TextField：multiline 1–4 行、fs13、py4、get3DInputStyle（src: profile-import-card.tsx:138-167）
  auto uf = std::make_unique<TextField>();
  url_field_ = uf.get();
  url_field_->set_placeholder(tr("settings.mini.importPlaceholder"));
  url_field_->set_i18n_a11y_name("settings.mini.importPlaceholder");
  url_field_->apply_theme();  // ← get3DInputStyle 6 套皮肤查表（:734-927）
  url_field_->style_.size = 13;
  url_field_->height_ = 46.0f;  // src: profile-import-card.tsx:145-165 py4 + 2×13px*1.4375 + py4 ≈ 46
  url_field_->multiline_ = true;  // src: profile-import-card.tsx:140 multiline minRows=2
  // 【保留】MUI OutlinedInput 缺省 radius 4：original/modern 由 apply_theme 给出
  //   8*d/6*d 更接近源码，此处不再覆盖。
  // 右键：5 项编辑命令菜单（_layout.tsx:1859-1932）
  url_field_->on_submit = [this] { do_import(); };
  url_field_->on_context_menu = [this](PointF anchor) {
    if (on_url_context_menu) on_url_context_menu(anchor);
  };
  add_child(std::move(uf));

  // 导入按钮：contained primary、fs12、h24、px2（:175-190）
  auto btn = std::make_unique<Button>();
  btn->set_i18n_text("settings.mini.importConfig");
  import_btn_ = btn.get();
  // src: button-styles.ts:17-491 get3DButtonStyle(theme,'contained','primary')
  //   —— 6 套皮肤各自一套（retro-3d 六停金渐变 :39，其余单色主色），
  //   改走 ThemeService 查表，切皮肤实时生效。
  btn->themed_ = true;
  btn->theme_color_type_ = "primary";
  btn->text_style.size = 12;
  btn->text_style.weight = 700;  // src: button-styles.ts:78 fontWeight 'bold'
  btn->text_style.align_h = AlignH::Center;
  btn->text_style.align_v = AlignV::Center;
  btn->on_click = [this] { do_import(); };
  add_child(std::move(btn));

  // 列表：maxHeight 110、gap 0.5（src: :194-201 overflowY auto → 滚动条可见）
  auto list = std::make_unique<ScrollList>();
  list_ = list.get();
  list_->set_show_scrollbar(true);
  list_->set_a11y_role(A11yRole::List);
  list_->set_a11y_labelled_by(title_);
  add_child(std::move(list));
}

ProfileImportCard::~ProfileImportCard() = default;

std::string ProfileImportCard::url() const {
  std::string s = utf::utf16_to_utf8(url_field_->text());
  auto b = s.find_first_not_of(" \t\r\n");
  auto e = s.find_last_not_of(" \t\r\n");
  return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

std::string ProfileImportCard::name() const {
  return "";  // 源码无名称输入（仅 URL 一个 TextField）
}

bool ProfileImportCard::url_valid() const {
  std::string u = url();
  std::string lower;
  for (char c : u) lower += static_cast<char>(std::tolower((unsigned char)c));
  return lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0;
}

void ProfileImportCard::do_import() {
  // ← Verge 前端口径：导入框同时接受 http(s) 订阅链接与直连节点，
  //   解析在命令链路（from_url → from_url_direct）完成。此处仅要求非空。
  const std::string target_url = url();
  if (target_url.empty()) return;
  if (import_btn_ && !import_btn_->enabled()) return;
  if (on_import_async) {
    if (url_field_) url_field_->set_enabled(false);
    if (import_btn_) import_btn_->set_enabled(false);
    on_import_async(target_url, name(), [this](bool ok) {
      if (url_field_) url_field_->set_enabled(true);
      if (import_btn_) import_btn_->set_enabled(true);
      if (ok) {
        if (url_field_) url_field_->clear_text();
        reload_profiles();
      }
      invalidate();
    });
    return;
  }
  bool ok = on_import ? on_import(target_url, name()) : false;
  if (ok) {
    if (url_field_) url_field_->clear_text();
    reload_profiles();
  }
}

void ProfileImportCard::set_profiles(const std::vector<config::PrfItem>& items,
                                     const std::string& current_uid) {
  profiles_ = items;
  current_uid_ = current_uid;
  rebuild_rows();
}

void ProfileImportCard::reload_profiles() {
  config::IProfiles p;
  try {
    p = config::IProfiles::load();
  } catch (...) {
    set_profiles({}, "");
    return;
  }
  std::vector<config::PrfItem> visible;
  if (p.items) {
    for (const auto& it : *p.items) {
      const auto t = it.type();
      if (t == config::PrfType::Remote || t == config::PrfType::Local) {
        visible.push_back(it);
      }
    }
  }
  set_profiles(visible, p.current.value_or(""));
}

ProfileRowWidget* ProfileImportCard::row_at(int i) const {
  if (!list_) return nullptr;
  return static_cast<ProfileRowWidget*>(list_->materialized_row(i));
}

void ProfileImportCard::rebuild_rows() {
  if (!list_) return;
  int n = static_cast<int>(profiles_.size());
  list_->set_content(n, kProfileRowH, [this](int index) {
    auto row = std::make_unique<ProfileRowWidget>();
    if (index >= 0 && index < static_cast<int>(profiles_.size())) {
      const auto& item = profiles_[index];
      row->bind(item, !current_uid_.empty() && item.uid.value_or("") == current_uid_);
      const std::string uid = item.uid.value_or("");
      row->on_select = [this, uid] {
        if (on_select_profile) on_select_profile(uid);
      };
      row->on_update = [this, uid] {
        if (on_update_profile) on_update_profile(uid);
      };
      row->on_delete = [this, uid] {
        if (on_delete_profile) on_delete_profile(uid);
      };
      row->on_context = [this, uid](PointF anchor) {
        if (on_context_menu) on_context_menu(uid, anchor);
      };
    }
    return row;
  }, 1);
}

const config::PrfItem* ProfileImportCard::find_item(const std::string& uid) const {
  for (const auto& it : profiles_) {
    if (it.uid && *it.uid == uid) return &it;
  }
  return nullptr;
}

float ProfileImportCard::preferred_height() const {
  const float list_h =
      std::min(kProfileListMaxH, kProfileRowH * static_cast<float>(profiles_.size()));
  return kProfileCardChromeH + list_h;
}

void ProfileImportCard::on_arrange(const RectF& area) {
  set_bounds(area);
  const float w = area.w;
  const float x = 8.0f;   // p1
  // src: profile-import-card.tsx:113-114 subtitle2 line-height 1.57 × 13px ≈ 20.4，mb 0.75=6（:110）
  title_->layout_tree({x, 8.0f, w - x * 2, 20.4f});
  // src: profile-import-card.tsx:145-165 multiline 2 行高（py4 + 2×13px*1.4375 + py4 ≈ 46）
  url_field_->layout_tree({x, 34.4f, w - x * 2, 46.0f});
  // 34.4+46+gap4+btn24+mb8（src: profile-import-card.tsx:130-136/:135/:192）
  const float list_y = 116.4f;
  // 按钮 px2=16 两侧 + 12px 文本约 72 → 宽 104（源码无固定宽，按内容居中）
  import_btn_->layout_tree({(w - 104.0f) / 2, 84.4f, 104.0f, 24.0f});
  float list_h = std::max(0.0f, area.h - list_y - 8.0f);
  list_->layout_tree({x, list_y, w - x * 2, list_h});
}

void ProfileImportCard::on_paint(IRenderTarget& rt) {
  // 卡片底 = get3DCardStyle('default')（src: button-styles.ts:492-731）：
  //   ★ 6 套皮肤逐皮肤分叉 —— retro-3d :534-541（135° 4 停金渐变）、
  //     original :572-606（白/#1e1f29）、modern-flat :611-648（白/#1f2937）、
  //     frosted-glass :653-669（半透明白）、cyberpunk :674-711（#fff/#05070c）、
  //     monochrome :716-728（纯黑/纯白）。
  //   统一走 ThemeService::card_paint()，不再各写一份硬编码金色。
  RectF r{0, 0, bounds().w, bounds().h};
  draw_card(rt, r, card_default());
  // 标题色：src: profile-import-card.tsx:121
  //   color: isRetro3DDark ? '#2C1F03' : 'inherit'
  //   'inherit' = 父级卡面的 color = get3DCardStyle(theme,'default').color
  //   → 即 card_paint("default").text_color（cyberpunk 暗态 #00f0ff、
  //     monochrome 纯黑/纯白，都不是 text.primary）。
  title_->style.color =
      title_color_.value_or(card_default().text_color);
}

}  // namespace clashwin::ui::scene

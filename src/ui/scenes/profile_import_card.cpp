// 订阅导入卡实现（T15 / T27：订阅列表 + 行内更新/删除 + 右键菜单入口）。
#include "profile_import_card.h"

#include <algorithm>
#include <ctime>

#include "../theme/theme.h"
#include "util/parse_traffic.h"
#include "util/utf.h"

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

}  // namespace

// ---------------- ProfileRowWidget ----------------
void ProfileRowWidget::bind(const config::PrfItem& item, bool active) {
  uid_ = item.uid.value_or("");
  name_ = item.name.value_or("");
  type_ = item.itype.value_or("");
  active_ = active;
  has_extra_ = item.extra.has_value();
  if (item.extra) {
    const auto& e = *item.extra;
    sub_line_ = "↑" + fmt_bytes(e.upload) + " ↓" + fmt_bytes(e.download) + " / " +
                fmt_bytes(e.total) + "   " + fmt_date(e.expire);
    progress_ = e.total > 0
                    ? static_cast<int>(std::min<uint64_t>(
                          100, (e.download + e.upload) * 100 / (e.total + 1)))
                    : 0;
  } else if (item.type() == config::PrfType::Local) {
    sub_line_ = "本地文件   " + fmt_date(static_cast<uint64_t>(item.updated.value_or(0)));
  }
  invalidate();
}

void ProfileRowWidget::on_paint(IRenderTarget& rt) {
  const auto& svc = ThemeService::instance();
  const auto& pal = svc.palette();
  const auto& vars = svc.vars();

  Brush bg{vars.theme_panel_bg};
  rt.round_rect({0, 0, bounds().w, bounds().h}, 5.0f,
                {vars.theme_border, 1.0f}, &bg);
  if (active_) {
    rt.fill_rect({0, 2, 3, bounds().h - 4}, Brush{pal.primary_color});
  }

  TextStyle name_ts;
  name_ts.size = 12;
  name_ts.weight = static_cast<int>(active_ ? Weight::Bold : Weight::Normal);
  name_ts.color = active_ ? pal.primary_color : pal.primary_text;
  name_ts.align_v = AlignV::Center;
  rt.draw_text(utf::utf8_to_utf16(name_.empty() ? "未命名配置" : name_),
               {8, 3, bounds().w - 54, 18}, name_ts);

  if (!sub_line_.empty()) {
    TextStyle sub_ts;
    sub_ts.size = 9.5f;
    sub_ts.color = pal.secondary_text;
    sub_ts.align_v = AlignV::Center;
    rt.draw_text(utf::utf8_to_utf16(sub_line_),
                 {8, 21, bounds().w - 54, 14}, sub_ts);
  }
  if (progress_ > 0) {
    rt.fill_rect({8, bounds().h - 5, bounds().w - 16, 2},
                 Brush{Color4f::hex(0x000000, 0.08f)});
    rt.fill_rect({8, bounds().h - 5,
                  (bounds().w - 16) * static_cast<float>(progress_) / 100.0f, 2},
                 Brush{pal.primary_color});
  }

  // 行内动作：↻ 更新（remote）/ ✕ 删除
  TextStyle act_ts;
  act_ts.size = 12;
  act_ts.align_h = AlignH::Center;
  act_ts.align_v = AlignV::Center;
  if (type_ == "remote") {
    act_ts.color = hover_update_ ? pal.primary_color : pal.secondary_text;
    rt.draw_text(L"↻", {bounds().w - 48, 0, 20, bounds().h}, act_ts);
  }
  act_ts.color = hover_delete_ ? Color4f::hex(0xFF3B30) : pal.secondary_text;
  rt.draw_text(L"✕", {bounds().w - 26, 0, 20, bounds().h}, act_ts);
}

bool ProfileRowWidget::on_mouse(const MouseEvent& e) {
  const bool over_update = type_ == "remote" && e.pos.x >= bounds().w - 52 &&
                           e.pos.x < bounds().w - 30;
  const bool over_delete = e.pos.x >= bounds().w - 30;
  if (e.type == MouseEventType::Move) {
    if (hover_update_ != over_update || hover_delete_ != over_delete) {
      hover_update_ = over_update;
      hover_delete_ = over_delete;
      invalidate();
    }
    return true;
  }
  if (e.type == MouseEventType::Down) {
    if (e.buttons & kMouseRight) {
      if (on_context) on_context();
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
    hover_update_ = hover_delete_ = false;
    invalidate();
  }
  return false;
}

// ---------------- ProfileImportCard ----------------
ProfileImportCard::ProfileImportCard(Widget* parent) : Widget(parent) {
  // 纵向：标题行 / URL 行 / 名称行 / 订阅列表 / 消息行
  auto title = std::make_unique<Label>();
  title_ = title.get();
  title_->style.size = 12;
  title_->style.weight = static_cast<int>(Weight::Bold);
  title_->set_text(L"订阅与机场配置");
  title_->flex_weight = 0;
  add_child(std::move(title));

  auto url_row = std::make_unique<Row>();
  url_row->spacing = 4;
  auto uf = std::make_unique<TextField>();
  url_field_ = uf.get();
  url_field_->flex_weight = 1;
  url_field_->set_placeholder(L"订阅链接...");
  url_row->add_child(std::move(uf));
  auto btn = std::make_unique<Button>(L"导入");
  import_btn_ = btn.get();
  btn->themed_ = true;
  btn->theme_color_type_ = "primary";
  btn->on_click = [this] { do_import(); };
  url_row->add_child(std::move(btn));
  url_row->flex_weight = 0;
  add_child(std::move(url_row));

  auto name_row = std::make_unique<Row>();
  name_row->spacing = 4;
  auto nf = std::make_unique<TextField>();
  name_field_ = nf.get();
  name_field_->flex_weight = 1;
  name_field_->set_placeholder(L"名称（可选）");
  name_row->add_child(std::move(nf));
  name_row->flex_weight = 0;
  add_child(std::move(name_row));

  auto list = std::make_unique<ScrollList>();
  list_ = list.get();
  list_->set_show_scrollbar(true);
  add_child(std::move(list));

  auto msg = std::make_unique<Label>();
  message_ = msg.get();
  message_->style.size = 10;
  message_->flex_weight = 0;
  add_child(std::move(msg));
}

ProfileImportCard::~ProfileImportCard() = default;

std::string ProfileImportCard::url() const {
  std::string s = utf::utf16_to_utf8(url_field_->text());
  auto b = s.find_first_not_of(" \t\r\n");
  auto e = s.find_last_not_of(" \t\r\n");
  return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

std::string ProfileImportCard::name() const {
  return utf::utf16_to_utf8(name_field_->text());
}

bool ProfileImportCard::url_valid() const {
  std::string u = url();
  std::string lower;
  for (char c : u) lower += static_cast<char>(std::tolower((unsigned char)c));
  return lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0;
}

void ProfileImportCard::do_import() {
  if (!url_valid()) {
    set_message(L"请输入有效的 http(s) 订阅链接", true);
    return;
  }
  bool ok = on_import ? on_import(url(), name()) : false;
  if (ok) {
    set_message(L"导入成功", false);
    reload_profiles();
  } else {
    set_message(L"导入失败（详见通知）", true);
  }
}

void ProfileImportCard::set_message(const std::wstring& msg, bool error) {
  message_->set_text(msg);
  msg_error_ = error;
  message_->style.color = error ? Color4f::hex(0xFF3B30) : Color4f::hex(0x06943D);
  message_->invalidate();
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
      row->on_context = [this, uid] {
        if (on_context_action) on_context_action("menu", uid);
      };
    }
    return row;
  }, 1);
}

void ProfileImportCard::on_arrange(const RectF& area) {
  set_bounds(area);
  float y = 0;
  title_->layout_tree({0, y, area.w, 20});
  y += 20;
  url_field_->layout_tree({0, y + 2, area.w - 62, 24});
  import_btn_->layout_tree({area.w - 58, y, 58, 28});
  y += 30;
  name_field_->layout_tree({0, y, area.w, 24});
  y += 28;
  float list_h = std::max(0.0f, area.h - y - 18.0f);
  list_->layout_tree({0, y, area.w, list_h});
  message_->layout_tree({0, area.h - 16, area.w, 16});
}

void ProfileImportCard::on_paint(IRenderTarget& rt) {
  const auto& svc = ThemeService::instance();
  Brush bg{svc.vars().theme_panel_bg};
  rt.round_rect({0, 0, bounds().w, bounds().h}, 6,
                {svc.vars().theme_border, 1}, &bg);
  title_->style.color =
      title_color_.value_or(svc.palette().primary_text);
}

}  // namespace clashwin::ui::scene

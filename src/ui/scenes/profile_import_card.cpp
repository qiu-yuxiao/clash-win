// 订阅导入卡实现（T15）。
#include "profile_import_card.h"

#include <algorithm>

#include "../theme/theme.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

namespace utf = clashwin::utf;
using theme::ThemeService;

ProfileImportCard::ProfileImportCard(Widget* parent) : Widget(parent) {
  // 纵向：标题行 / URL 行 / 名称行 / 消息行（总高 ~88px 内，240px 宽）
  auto title = std::make_unique<Label>();
  title_ = title.get();
  title_->style.size = 12;
  title_->style.weight = static_cast<int>(Weight::Bold);
  title_->set_text(L"导入订阅");  // ← i18n profiles.title 同义
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

  auto msg = std::make_unique<Label>();
  message_ = msg.get();
  message_->style.size = 11;
  message_->flex_weight = 1;
  add_child(std::move(msg));
}

ProfileImportCard::~ProfileImportCard() = default;

std::string ProfileImportCard::url() const {
  std::string s = utf::utf16_to_utf8(url_field_->text());
  // trim
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
  // http(s) 粗校验；SSRF/脏 URL 细校验在命令链路（fix_dirty_url+SSRF）
  return lower.rfind("http://", 0) == 0 || lower.rfind("https://", 0) == 0;
}

void ProfileImportCard::do_import() {
  if (!url_valid()) {
    set_message(L"请输入有效的 http(s) 订阅链接", true);
    return;
  }
  if (!on_import || on_import(url(), name()))
    set_message(L"导入成功", false);
  else
    set_message(L"导入失败（详见通知）", true);
}

void ProfileImportCard::set_message(const std::wstring& msg, bool error) {
  message_->set_text(msg);
  msg_error_ = error;
  message_->style.color = error ? Color4f::hex(0xFF3B30) : Color4f::hex(0x06943D);
  message_->invalidate();
}

void ProfileImportCard::on_arrange(const RectF& area) {
  set_bounds(area);
  // 【坐标约定】子布局局部坐标
  float y = 0;
  title_->layout_tree({0, y, area.w, 20});
  y += 20;
  url_field_  // url 行：字段 flex + 按钮 56
      ->layout_tree({0, y + 2, area.w - 62, 24});
  import_btn_->layout_tree({area.w - 58, y, 58, 28});
  y += 30;
  name_field_->layout_tree({0, y, area.w, 24});
  y += 26;
  message_->layout_tree({0, y, area.w, std::max(area.h - y, 16.0f)});
}

void ProfileImportCard::on_paint(IRenderTarget& rt) {
  // 卡片底（← get3DCardStyle default 近似：浅底 + 圆角）；局部坐标 {0,0,w,h}
  const auto& svc = ThemeService::instance();
  Brush bg{svc.vars().theme_panel_bg};
  rt.round_rect({0, 0, bounds().w, bounds().h}, 6,
                {svc.vars().theme_border, 1}, &bg);
  title_->style.color =
      title_color_.value_or(svc.palette().primary_text);  // 抽屉可覆盖为橙
  // message 色由 set_message 维护
}

}  // namespace clashwin::ui::scene

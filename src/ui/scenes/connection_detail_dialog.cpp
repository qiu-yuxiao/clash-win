#include "connection_detail_dialog.h"

#include <windows.h>
#include <algorithm>

#include "ui/theme/theme.h"
#include "util/parse_traffic.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

ConnectionDetailDialog::ConnectionDetailDialog(Widget* parent) : Widget(parent) {
  set_z_order(950);
}

void ConnectionDetailDialog::open(const ConnItem& item, bool closed) {
  item_ = item;
  closed_ = closed;
  open_ = true;
  invalidate();
}

void ConnectionDetailDialog::close() {
  if (!open_) return;
  open_ = false;
  hover_close_conn_ = false;
  hover_dismiss_ = false;
  invalidate();
  if (on_dismiss) on_dismiss();
}

std::vector<std::pair<std::wstring, std::wstring>> ConnectionDetailDialog::fields() const {
  std::vector<std::pair<std::wstring, std::wstring>> res;

  // 1. Host
  res.emplace_back(L"域名/地址", utf::utf8_to_utf16(item_.host_formatted()));

  // 2. Downloaded
  auto [dv, du] = util::parse_traffic(item_.download);
  res.emplace_back(L"累计下载", utf::utf8_to_utf16(dv + " " + du));

  // 3. Uploaded
  auto [uv, uu] = util::parse_traffic(item_.upload);
  res.emplace_back(L"累计上传", utf::utf8_to_utf16(uv + " " + uu));

  // 4. Dl Speed
  auto [dsv, dsu] = util::parse_traffic(item_.cur_download);
  res.emplace_back(L"下载速率", utf::utf8_to_utf16(dsv + " " + dsu + "/s"));

  // 5. Ul Speed
  auto [usv, usu] = util::parse_traffic(item_.cur_upload);
  res.emplace_back(L"上传速率", utf::utf8_to_utf16(usv + " " + usu + "/s"));

  // 6. Chains
  res.emplace_back(L"规则链", utf::utf8_to_utf16(item_.chains_formatted()));

  // 7. Rule
  res.emplace_back(L"命中规则", utf::utf8_to_utf16(item_.rule_formatted()));

  // 8. Process
  res.emplace_back(L"关联进程", utf::utf8_to_utf16(item_.process_formatted()));

  // 9. Time
  res.emplace_back(L"建立时间", utf::utf8_to_utf16(item_.start));

  // 10. Source
  std::string src = item_.metadata.source_ip;
  if (!item_.metadata.source_port.empty()) {
    src += ":" + item_.metadata.source_port;
  }
  res.emplace_back(L"来源", utf::utf8_to_utf16(src));

  // 11. Destination
  std::string dst = item_.metadata.destination_ip.empty() ? item_.metadata.remote_destination
                                                          : item_.metadata.destination_ip;
  res.emplace_back(L"目的", utf::utf8_to_utf16(dst));

  // 12. Destination Port
  res.emplace_back(L"目的端口", utf::utf8_to_utf16(item_.metadata.destination_port));

  // 13. Type
  std::string typ = item_.metadata.type;
  if (!item_.metadata.network.empty()) {
    typ += "(" + item_.metadata.network + ")";
  }
  res.emplace_back(L"类型/网络", utf::utf8_to_utf16(typ));

  return res;
}

void ConnectionDetailDialog::on_arrange(const RectF& area) {
  set_bounds(area);
  float cw = std::min(area.w - 24.0f, 350.0f);
  float ch = std::min(area.h - 24.0f, 360.0f);
  if (cw < 180.0f) cw = area.w;
  if (ch < 200.0f) ch = area.h;
  float cx = (area.w - cw) / 2.0f;
  float cy = (area.h - ch) / 2.0f;
  card_rect_ = {cx, cy, cw, ch};

  float btn_y = cy + ch - 30.0f;
  btn_dismiss_rect_ = {cx + cw - 62.0f, btn_y, 50.0f, 22.0f};
  btn_close_conn_rect_ = {cx + cw - 146.0f, btn_y, 76.0f, 22.0f};
}

void ConnectionDetailDialog::on_paint(IRenderTarget& rt) {
  if (!open_) return;

  // 1. 半透明遮罩（局部坐标 {0,0,w,h}，绝不用 bounds().x/y）
  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{Color4f(0, 0, 0, 0.45f)});

  const auto& theme = theme::ThemeService::instance();
  const auto& pal = theme.palette();
  const auto& vars = theme.vars();

  // 2. 居中卡片底（阴影 + 纸底 + 圆角 8）
  Brush shadow_b{Color4f(0, 0, 0, 0.25f)};
  rt.round_rect({card_rect_.x - 2, card_rect_.y - 1, card_rect_.w + 4, card_rect_.h + 5}, 9.0f,
                {}, &shadow_b);

  Brush card_bg{vars.theme_panel_bg};
  rt.round_rect(card_rect_, 8.0f, {vars.divider_color, 1.0f}, &card_bg);

  // 3. 标题
  TextStyle title_ts;
  title_ts.size = 12.5f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = pal.primary_text;
  title_ts.align_v = AlignV::Center;
  rt.draw_text(L"连接详情", {card_rect_.x + 14, card_rect_.y + 8, card_rect_.w - 28, 20},
               title_ts);

  // 分隔线
  rt.fill_rect({card_rect_.x + 10, card_rect_.y + 32, card_rect_.w - 20, 1}, Brush{vars.divider_color});

  // 4. 13 项元数据逐行绘制
  auto f_list = fields();
  float row_y = card_rect_.y + 36.0f;
  const float row_h = 19.5f;
  TextStyle label_ts;
  label_ts.size = 10.5f;
  label_ts.weight = static_cast<int>(Weight::Bold);
  label_ts.color = pal.secondary_text;
  label_ts.align_v = AlignV::Center;

  TextStyle val_ts;
  val_ts.size = 10.5f;
  val_ts.color = pal.primary_text;
  val_ts.align_v = AlignV::Center;
  val_ts.ellipsis = true;

  for (const auto& [lbl, val] : f_list) {
    if (row_y + row_h > card_rect_.y + card_rect_.h - 34.0f) break;
    rt.draw_text(lbl + L":", {card_rect_.x + 14, row_y, 72, row_h}, label_ts);
    rt.draw_text(val, {card_rect_.x + 88, row_y, card_rect_.w - 102, row_h}, val_ts);
    row_y += row_h;
  }

  // 5. 底部按钮
  if (!closed_) {
    // 断开连接按钮（错误色红底或描边）
    Color4f btn_bg = hover_close_conn_ ? Color4f::hex(0xD32F2F) : Color4f::hex(0xEF5350);
    Brush b_bg{btn_bg};
    rt.round_rect(btn_close_conn_rect_, 4.0f, {}, &b_bg);
    TextStyle btn_ts;
    btn_ts.size = 11.0f;
    btn_ts.color = Color4f(1, 1, 1, 1);
    btn_ts.align_h = AlignH::Center;
    btn_ts.align_v = AlignV::Center;
    rt.draw_text(L"断开连接", btn_close_conn_rect_, btn_ts);
  }

  // 关闭按钮
  Color4f dis_bg = hover_dismiss_ ? Color4f::hex(0x000000, 0.12f) : Color4f::hex(0x000000, 0.06f);
  Brush d_bg{dis_bg};
  rt.round_rect(btn_dismiss_rect_, 4.0f, {vars.divider_color, 1.0f}, &d_bg);
  TextStyle dis_ts;
  dis_ts.size = 11.0f;
  dis_ts.color = pal.primary_text;
  dis_ts.align_h = AlignH::Center;
  dis_ts.align_v = AlignV::Center;
  rt.draw_text(L"关闭", btn_dismiss_rect_, dis_ts);
}

bool ConnectionDetailDialog::on_mouse(const MouseEvent& e) {
  if (!open_) return false;

  PointF pt = e.pos;

  if (e.type == MouseEventType::Move) {
    bool hc = !closed_ && btn_close_conn_rect_.contains(pt.x, pt.y);
    bool hd = btn_dismiss_rect_.contains(pt.x, pt.y);
    if (hc != hover_close_conn_ || hd != hover_dismiss_) {
      hover_close_conn_ = hc;
      hover_dismiss_ = hd;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (!closed_ && btn_close_conn_rect_.contains(pt.x, pt.y)) {
      if (on_close_connection) on_close_connection(item_.id);
      close();
      return true;
    }
    if (btn_dismiss_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    // 点击卡片外遮罩：关闭
    if (!card_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    return true;  // 吞掉卡片内点击
  }

  if (e.type == MouseEventType::Down) {
    return true;  // 吞掉下层事件
  }

  return true;
}

bool ConnectionDetailDialog::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down && (e.key == VK_ESCAPE || e.key == 27)) {
    close();
    return true;
  }
  return false;
}

}  // namespace clashwin::ui::scene

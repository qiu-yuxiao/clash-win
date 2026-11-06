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

void ConnectionDetailDialog::open(const ConnItem& item, bool closed, const RectF& anchor_global) {
  item_ = item;
  closed_ = closed;
  anchor_global_ = anchor_global;
  open_ = true;
  if (bounds().w > 0 && bounds().h > 0) place_card();
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

  // 字段标签与取值顺序照 Mini：src: connection-detail.tsx:104-151
  //   标签源 locales/zh/connections.json:7-17（下载量/上传量见 shared.json:45-46）

  // 1. host → 主机
  res.emplace_back(L"主机", utf::utf8_to_utf16(item_.host_formatted()));

  // 2. downloaded → 下载量（shared.json:45）
  auto [dv, du] = util::parse_traffic(item_.download);
  res.emplace_back(L"下载量", utf::utf8_to_utf16(dv + " " + du));

  // 3. uploaded → 上传量（shared.json:46）
  auto [uv, uu] = util::parse_traffic(item_.upload);
  res.emplace_back(L"上传量", utf::utf8_to_utf16(uv + " " + uu));

  // 4. dlSpeed → 下载速度
  auto [dsv, dsu] = util::parse_traffic(item_.cur_download);
  res.emplace_back(L"下载速度", utf::utf8_to_utf16(dsv + " " + dsu + "/s"));

  // 5. ulSpeed → 上传速度
  auto [usv, usu] = util::parse_traffic(item_.cur_upload);
  res.emplace_back(L"上传速度", utf::utf8_to_utf16(usv + " " + usu + "/s"));

  // 6. chains → 链路
  res.emplace_back(L"链路", utf::utf8_to_utf16(item_.chains_formatted()));

  // 7. rule → 规则
  res.emplace_back(L"规则", utf::utf8_to_utf16(item_.rule_formatted()));

  // 8. process → 进程
  res.emplace_back(L"进程", utf::utf8_to_utf16(item_.process_formatted()));

  // 9. time → 连接时间
  res.emplace_back(L"连接时间", utf::utf8_to_utf16(item_.start));

  // 10. source → 源地址
  std::string src = item_.metadata.source_ip;
  if (!item_.metadata.source_port.empty()) {
    src += ":" + item_.metadata.source_port;
  }
  res.emplace_back(L"源地址", utf::utf8_to_utf16(src));

  // 11. destination → 目标地址
  std::string dst = item_.metadata.destination_ip.empty() ? item_.metadata.remote_destination
                                                          : item_.metadata.destination_ip;
  res.emplace_back(L"目标地址", utf::utf8_to_utf16(dst));

  // 12. destinationPort → 目标端口
  res.emplace_back(L"目标端口", utf::utf8_to_utf16(item_.metadata.destination_port));

  // 13. type → 类型
  std::string typ = item_.metadata.type;
  if (!item_.metadata.network.empty()) {
    typ += "(" + item_.metadata.network + ")";
  }
  res.emplace_back(L"类型", utf::utf8_to_utf16(typ));

  return res;
}

void ConnectionDetailDialog::on_arrange(const RectF& area) {
  set_bounds(area);
  place_card();
}

void ConnectionDetailDialog::place_card() {
  const RectF area = bounds();
  const float kMargin = 16.0f;  // src: MUI Popper marginThreshold 默认 16
  // Mini 纸面 maxWidth 520 / maxHeight 480，内容自适应（overflowY auto）
  // src: connection-detail.tsx:59-61
  const float row_h = 24.0f;  // 12px×行高1.5(18) + gap 0.75(6)：:160/:163
  const float content_h = 36.0f + 13 * row_h + 34.0f;  // 标题/13 行/底部按钮
  float cw = std::min(area.w - 2 * kMargin, 520.0f);
  float ch = std::min(area.h - 2 * kMargin, std::min(480.0f, content_h));
  if (cw < 180.0f) cw = area.w;
  if (ch < 200.0f) ch = area.h;

  float x = 0.0f;
  float y = 0.0f;
  float ax = 0.0f, ay = 0.0f, ah = 0.0f, aw = 0.0f;
  if (anchor_global_.w > 0 && anchor_global_.h > 0) {
    // src: connection-detail.tsx:48-55 anchorOrigin bottom/left + transformOrigin top/left
    // → 首选：左上角贴在触发行左下角。
    PointF tl = to_local({anchor_global_.x, anchor_global_.y});
    ax = tl.x;
    ay = tl.y;
    ah = anchor_global_.h;
    aw = anchor_global_.w;

    // 垂直：下方放不下则翻到上方（MUI Popper flip）
    y = ay + ah;
    if (y + ch > area.h - kMargin) y = ay - ch;
    // 水平：右侧放不下则以锚点右边缘右对齐（向左翻）
    x = ax;
    if (x + cw > area.w - kMargin) x = ax + aw - cw;

    // 最终夹在面板范围内（MUI preventOverflow；面板本身已在窗口内留边）
    x = std::clamp(x, 0.0f, std::max(0.0f, area.w - cw));
    y = std::clamp(y, 0.0f, std::max(0.0f, area.h - ch));
  } else {
    // 无锚点（单元测试）：居中
    x = (area.w - cw) / 2.0f;
    y = (area.h - ch) / 2.0f;
  }
  card_rect_ = {x, y, cw, ch};

  float btn_y = y + ch - 30.0f;
  btn_dismiss_rect_ = {x + cw - 62.0f, btn_y, 50.0f, 22.0f};
  btn_close_conn_rect_ = {x + cw - 146.0f, btn_y, 76.0f, 22.0f};
}

void ConnectionDetailDialog::on_paint(IRenderTarget& rt) {
  if (!open_) return;

  // Mini 的连接详情是 MUI Popover（src/components/connection/connection-detail.tsx:43-47），
  // Popover 的 Backdrop 为 invisible（@mui/material/Popover/Popover.js:337）→ 无可见遮罩。
  // 点外部关闭行为保留（本文件 :214）。

  const auto& theme = theme::ThemeService::instance();
  const auto& pal = theme.palette();
  const auto& vars = theme.vars();

  // 2. 卡片底（阴影 + 纸底 + 圆角 8；位置由 place_card 按触发行锚定）
  Brush shadow_b{Color4f(0, 0, 0, 0.25f)};  // 豁免: Mini 无对应物 — D2D 无 box-shadow，投影用偏移层模拟（出处: connection-detail.tsx:59-61 无阴影色值）
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
  const float row_h = 24.0f;  // src: connection-detail.tsx:160（12px×1.5）+ :163 gap 6
  TextStyle label_ts;
  label_ts.size = 12.0f;  // src: :160 fontSize 12
  label_ts.weight = static_cast<int>(Weight::Bold);  // src: :168 <b>
  label_ts.color = pal.secondary_text;  // src: :159
  label_ts.align_v = AlignV::Center;

  TextStyle val_ts;
  val_ts.size = 12.0f;  // src: :160
  val_ts.color = pal.primary_text;  // src: :174
  val_ts.align_v = AlignV::Center;
  val_ts.ellipsis = true;

  for (const auto& [lbl, val] : f_list) {
    if (row_y + row_h > card_rect_.y + card_rect_.h - 34.0f) break;
    // label minWidth 100 + gap 1(=8px) → value 起点 = label 起点 + 108
    // src: connection-detail.tsx:167-168
    rt.draw_text(lbl + L":", {card_rect_.x + 14, row_y, 100, row_h}, label_ts);
    rt.draw_text(val, {card_rect_.x + 122, row_y, card_rect_.w - 136, row_h}, val_ts);
    row_y += row_h;
  }

  // 5. 底部按钮
  if (!closed_) {
    // 断开连接按钮（错误色红底或描边）
    Color4f btn_bg = hover_close_conn_ ? Color4f::hex(0xD32F2F)  // src: MUI red[700]
                                        : Color4f::hex(0xEF5350);  // src: MUI red[400]
    Brush b_bg{btn_bg};
    rt.round_rect(btn_close_conn_rect_, 4.0f, {}, &b_bg);
    TextStyle btn_ts;
    btn_ts.size = 11.0f;
    btn_ts.color = Color4f(1, 1, 1, 1);  // src: MUI Button contrastText #fff
    btn_ts.align_h = AlignH::Center;
    btn_ts.align_v = AlignV::Center;
    // src: locales/zh/connections.json:28 「"closeConnection": "关闭连接"」（connection-detail.tsx:196）
    rt.draw_text(L"关闭连接", btn_close_conn_rect_, btn_ts);
  }

  // 关闭按钮
  Color4f dis_bg = hover_dismiss_ ? Color4f::hex(0x000000, 0.12f)  // 豁免: 关闭钮悬停 12% 黑
                                   : Color4f::hex(0x000000, 0.06f);  // 豁免: 关闭钮常态 6% 黑
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

#include "connection_detail_dialog.h"

#include <windows.h>
#include <algorithm>

#include "ui/button_draw.h"
#include "ui/card_draw.h"
#include "ui/soft_shadow.h"
#include "ui/theme/theme.h"
#include "util/parse_traffic.h"
#include "util/utf.h"
#include "ui/i18n_text.h"  // F-D01
#include "core/connection_stale.h"  // BUG-396

namespace clashwin::ui::scene {

namespace {
// ---------- 连接详情浮层几何（← components/connection/connection-detail.tsx）----------
// 该浮层是 MUI Popover：paper sx = getPopupCardStyle + maxWidth 520 / maxHeight 480
// / overflowY auto / p 2（:56-62）。内容只有字段行与底部关闭区，没有标题与分隔线。
constexpr float kDetailPad = 16.0f;      // ← connection-detail.tsx:61 p: 2
constexpr float kDetailRowH = 12.0f * 1.4f;  // ← :157 fontSize 12 × body1 行高 1.4
constexpr float kDetailGap = 6.0f;       // ← :157 gap: 0.75
constexpr float kDetailLabelW = 100.0f;  // ← :212 <b style={{ minWidth: 100 }}>
constexpr float kDetailBtnPadX = 10.0f;  // ← MUI Button size="small" padding '4px 10px'
constexpr float kDetailBtnPadY = 4.0f;

std::wstring upper_ascii_w(const std::wstring& s) {
  std::wstring out = s;
  for (wchar_t& c : out) {
    if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
  }
  return out;
}
}  // namespace

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

  // 1. host → 主机（空兜底 "--"，src: connection-detail.tsx:97-105）
  std::string raw_host = item_.metadata.host;
  if (raw_host.empty()) raw_host = item_.metadata.remote_destination;
  if (raw_host.empty()) raw_host = item_.metadata.destination_ip;
  if (raw_host.empty()) raw_host = item_.metadata.process;
  if (raw_host.empty()) raw_host = "--";
  std::string host_disp = item_.metadata.destination_port.empty()
                              ? raw_host
                              : (raw_host + ":" + item_.metadata.destination_port);
  res.emplace_back(tr("connections.components.fields.host"), utf::utf8_to_utf16(host_disp));

  // BUG-396（connection-detail.tsx:106-143）：主机下方新增"代理节点"行。
  // 直连显示 DIRECT、封锁显示本地化标签，旧节点追加"（旧节点连接）"标记。
  const auto nd = core::resolve_connection_node_display(
      item_.chains, item_.is_synthetic, closed_ || item_.is_closed,
      current_proxy_now_);
  std::wstring proxy_node_val = L"--";
  switch (nd.kind) {
    case core::ConnectionNodeDisplay::Kind::Proxy: {
      std::wstring v = utf::utf8_to_utf16(nd.node_name);
      if (nd.is_stale_proxy_node) {
        v += L" (" + tr("connections.components.fields.staleNodeTag") + L")";
      }
      proxy_node_val = std::move(v);
      break;
    }
    case core::ConnectionNodeDisplay::Kind::Direct: {
      const std::wstring label =
          tr("connections.components.miniTable.chainsDirect");
      proxy_node_val = upper_ascii_w(label) == L"DIRECT"
                           ? std::wstring(L"DIRECT")
                           : (label + L" (DIRECT)");
      break;
    }
    case core::ConnectionNodeDisplay::Kind::Reject: {
      const std::wstring label =
          tr("connections.components.miniTable.chainsReject");
      const std::wstring name = utf::utf8_to_utf16(nd.node_name);
      proxy_node_val = upper_ascii_w(label) == upper_ascii_w(name)
                           ? name
                           : (label + L" (" + name + L")");
      break;
    }
    case core::ConnectionNodeDisplay::Kind::None:
    default:
      break;
  }
  res.emplace_back(tr("connections.components.fields.proxyNode"), proxy_node_val);

  // 2. downloaded → 下载量（shared.json:45）
  auto [dv, du] = util::parse_traffic(item_.download);
  res.emplace_back(tr("shared.labels.downloaded"), utf::utf8_to_utf16(dv + " " + du));

  // 3. uploaded → 上传量（shared.json:46）
  auto [uv, uu] = util::parse_traffic(item_.upload);
  res.emplace_back(tr("shared.labels.uploaded"), utf::utf8_to_utf16(uv + " " + uu));

  // 4. dlSpeed → 下载速度
  auto [dsv, dsu] = util::parse_traffic(item_.cur_download);
  res.emplace_back(tr("connections.components.fields.dlSpeed"), utf::utf8_to_utf16(dsv + " " + dsu + "/s"));

  // 5. ulSpeed → 上传速度
  auto [usv, usu] = util::parse_traffic(item_.cur_upload);
  res.emplace_back(tr("connections.components.fields.ulSpeed"), utf::utf8_to_utf16(usv + " " + usu + "/s"));

  // 6. chains → 链路
  res.emplace_back(tr("connections.components.fields.chains"), utf::utf8_to_utf16(item_.chains_formatted()));

  // 7. rule → 规则
  res.emplace_back(tr("connections.components.fields.rule"), utf::utf8_to_utf16(item_.rule_formatted()));

  // 8. process → 进程
  res.emplace_back(tr("connections.components.fields.process"), utf::utf8_to_utf16(item_.process_formatted()));

  // 9. time → 连接时间
  res.emplace_back(tr("connections.components.fields.time"),
                   utf::utf8_to_utf16(item_.start.empty() ? std::string("--") : item_.start));

  // 10. source → 源地址
  std::string src = item_.metadata.source_ip.empty()
                        ? std::string("--")
                        : (item_.metadata.source_ip +
                           (item_.metadata.source_port.empty()
                                ? std::string()
                                : (":" + item_.metadata.source_port)));
  res.emplace_back(tr("connections.components.fields.source"), utf::utf8_to_utf16(src));

  // 11. destination → 目标地址
  std::string dst = !item_.metadata.destination_ip.empty()
                        ? item_.metadata.destination_ip
                        : (!item_.metadata.remote_destination.empty()
                               ? item_.metadata.remote_destination
                               : std::string("--"));
  res.emplace_back(tr("connections.components.fields.destination"), utf::utf8_to_utf16(dst));

  // 12. destinationPort → 目标端口
  res.emplace_back(tr("connections.components.fields.destinationPort"), utf::utf8_to_utf16(item_.metadata.destination_port));

  // 13. type → 类型
  std::string typ = item_.metadata.type;
  if (!item_.metadata.network.empty()) {
    typ += "(" + item_.metadata.network + ")";
  }
  res.emplace_back(tr("connections.components.fields.type"), utf::utf8_to_utf16(typ));

  return res;
}

bool ConnectionDetailDialog::is_stale_proxy_node() const {
  return core::resolve_connection_node_display(
      item_.chains, item_.is_synthetic, closed_ || item_.is_closed,
      current_proxy_now_)
      .is_stale_proxy_node;
}

void ConnectionDetailDialog::on_arrange(const RectF& area) {
  set_bounds(area);
  place_card();
}

void ConnectionDetailDialog::place_card() {
  const RectF area = bounds();
  const float kMargin = 16.0f;  // src: MUI Popper marginThreshold 默认 16
  // src: connection-detail.tsx:56-62 纸面 maxWidth 520 / maxHeight 480 / p 2(16)；
  // 内容 = 字段行（根 Box fontSize 12 / body1 行高 1.4 / gap 0.75）+ 底部关闭区
  constexpr float kPad = kDetailPad;
  constexpr float kRowH = kDetailRowH;
  constexpr float kGap = kDetailGap;
  const float rows = static_cast<float>(fields().size());
  const bool can_close_conn = !closed_ && !item_.is_synthetic;
  // 关闭区 ← :196-199：mt 1(8) + pt 1.25(10) + borderTop 1px，内含 size="small" 按钮
  // （MUI Button small：padding '4px 10px' / lineHeight 1.75 / 字号沿用 13px）
  const auto err_paint = theme::ThemeService::instance().button_paint("error");
  const float btn_h = 13.0f * 1.75f + kDetailBtnPadY * 2.0f + err_paint.border_w * 2.0f;
  const float close_sec_h = can_close_conn ? (8.0f + 10.0f + 1.0f + btn_h) : 0.0f;
  // 此前这里把「查看详情」标题与分隔线计入高度（36/34 的魔法数），Mini 的 Popover
  // 里两者都不存在（:207-247 只有字段行 + 关闭区）。
  float content_h = kPad * 2.0f + rows * kRowH + std::max(0.0f, rows - 1.0f) * kGap;
  if (can_close_conn) content_h += kGap + close_sec_h;

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

  // 关闭连接按钮：右对齐（← :196 textAlign: right），宽 = 文字 + px 10×2（small 按钮）
  float btn_w = 0.0f;
  const std::wstring btn_label = tr("connections.components.actions.closeConnection");
  if (ITextEngine* e = text_engine()) {
    TextStyle ts;
    ts.size = 13.0f;
    ts.weight = err_paint.weight;
    btn_w = e->measure_text(btn_label, ts, 0).w;
  }
  btn_w = std::min(btn_w + kDetailBtnPadX * 2.0f, std::max(40.0f, cw - kPad * 2.0f));
  const float btn_y = y + ch - kPad - btn_h;
  btn_close_conn_rect_ = {x + cw - kPad - btn_w, btn_y, btn_w, btn_h};
  btn_dismiss_rect_ = {0, 0, 0, 0};  // Mini 无独立关闭按钮（点外部关闭）
}

void ConnectionDetailDialog::on_paint(IRenderTarget& rt) {
  if (!open_) return;

  // Mini 的连接详情是 MUI Popover（src/components/connection/connection-detail.tsx:43-47），
  // Popover 的 Backdrop 为 invisible（@mui/material/Popover/Popover.js:337）→ 无可见遮罩。
  // 点外部关闭行为保留（本文件 :214）。

  const auto& theme = theme::ThemeService::instance();
  const auto tokens = theme.popup_tokens();
  const auto card_paint = theme.popup_card_paint();

  // 2. 卡片底（统一走 popup_card_paint()，支持 6 皮肤及 frosted-glass 实底防穿透；位置由 place_card 按触发行锚定）
  if (!card_paint.has_shadow && !card_paint.has_glow) {
    Brush shadow_b{Color4f(0, 0, 0, 0.25f)};  // 豁免: Mini 无对应物 — retro 之外无阴影时保留 Popover 浅投影（出处: 无）
    rt.round_rect({card_rect_.x - 2, card_rect_.y - 1, card_rect_.w + 4, card_rect_.h + 5},
                  card_paint.radius + 1.0f, {}, &shadow_b);
  }
  ::clashwin::ui::draw_card(rt, card_rect_, card_paint);

  // 3. 字段行（← connection-detail.tsx:207-247：Popover 里没有标题、没有分隔线，
  //    p 2 之后直接就是字段列表）
  auto f_list = fields();
  float row_y = card_rect_.y + kDetailPad;
  const float row_h = kDetailRowH;
  const float label_x = card_rect_.x + kDetailPad;
  const float value_x = label_x + kDetailLabelW + 8.0f;  // ← :211 gap: 1(8)
  const float value_w = std::max(20.0f, card_rect_.w - kDetailPad * 2.0f -
                                           kDetailLabelW - 8.0f);
  TextStyle label_ts;
  label_ts.size = 12.0f;  // src: :157 fontSize 12
  label_ts.weight = static_cast<int>(Weight::Bold);  // src: :212 <b>
  label_ts.color = tokens.secondary_text;  // src: :159
  label_ts.align_v = AlignV::Center;

  TextStyle val_ts;
  val_ts.size = 12.0f;  // src: :157
  val_ts.color = tokens.primary_text;  // src: :231
  val_ts.align_v = AlignV::Center;
  val_ts.ellipsis = true;

  const bool stale_warning = is_stale_proxy_node();
  const bool can_close_conn = !closed_ && !item_.is_synthetic;
  const float rows_bottom =
      can_close_conn ? btn_close_conn_rect_.y - 11.0f  // ← :198 pt 1.25(10) + borderTop 1px
                     : card_rect_.bottom() - kDetailPad;
  size_t row_idx = 0;
  for (const auto& [lbl, val] : f_list) {
    if (row_y + row_h > rows_bottom) break;
    // label minWidth 100 + gap 1(=8px) → value 起点 = label 起点 + 108
    // src: connection-detail.tsx:211-212
    rt.draw_text(lbl + L":", {label_x, row_y, kDetailLabelW, row_h}, label_ts);
    // BUG-396（connection-detail.tsx:219-224）：旧节点连接值用 warning 色 + 600 字重
    TextStyle row_val_ts = val_ts;
    if (row_idx == 1 && stale_warning) {
      row_val_ts.color = tokens.warning_color;
      row_val_ts.weight = 600;
    }
    rt.draw_text(val, {value_x, row_y, value_w, row_h}, row_val_ts);
    row_y += row_h + kDetailGap;
    ++row_idx;
  }

  // 4. 关闭区：上边框 + 右对齐按钮（← connection-detail.tsx:196-245）
  if (can_close_conn) {
    const float border_y = btn_close_conn_rect_.y - 11.0f;
    rt.fill_rect({card_rect_.x + kDetailPad, border_y,
                  card_rect_.w - kDetailPad * 2.0f, 1.0f},
                 Brush{tokens.divider_color});
    // 按钮走 button_paint("error")：6 套皮肤的红底/描边/字色/浮雕全部查表
    const auto bp = theme::ThemeService::instance().button_paint("error");
    const RectF& r = btn_close_conn_rect_;
    draw_button_skin(rt, r, bp, hover_close_conn_ ? 1.0f : 0.0f, /*pressed=*/false);
    TextStyle btn_ts;
    btn_ts.size = 13.0f;  // ← MUI Button size="small" 字号
    btn_ts.weight = bp.weight;
    btn_ts.color = bp.text;
    btn_ts.align_h = AlignH::Center;
    btn_ts.align_v = AlignV::Center;
    // src: locales/zh/connections.json:28 「"closeConnection": "关闭连接"」（connection-detail.tsx:196）
    rt.draw_text(tr("connections.components.actions.closeConnection"), r, btn_ts);
  }
}

bool ConnectionDetailDialog::on_mouse(const MouseEvent& e) {
  if (!open_) return false;

  PointF pt = e.pos;

  if (e.type == MouseEventType::Move) {
    const bool can_close_conn = !closed_ && !item_.is_synthetic;
    bool hc = can_close_conn && btn_close_conn_rect_.contains(pt.x, pt.y);
    if (hc != hover_close_conn_) {
      hover_close_conn_ = hc;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    const bool can_close_conn = !closed_ && !item_.is_synthetic;
    if (can_close_conn && btn_close_conn_rect_.contains(pt.x, pt.y)) {
      if (on_close_connection) on_close_connection(item_.id);
      close();
      return true;
    }
    // 点击卡片外遮罩：关闭（← Popover 的 click-away；Mini 无独立关闭按钮）
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

std::wstring ConnectionDetailDialog::a11y_default_name() const {
  return tr("connections.components.contextMenu.viewDetail") + L" - " +
         utf::utf8_to_utf16(item_.host_formatted());
}

size_t ConnectionDetailDialog::a11y_virtual_child_count() const {
  const bool can_close_conn = !closed_ && !item_.is_synthetic;
  // 摘要文本 + 关闭连接按钮；Mini 的 Popover 里没有独立「关闭」按钮
  return can_close_conn ? 2u : 1u;
}

A11ySubItem ConnectionDetailDialog::a11y_virtual_child(size_t idx) const {
  A11ySubItem sub;
  if (idx == 0) {
    sub.role = A11yRole::Text;
    std::wstring summary;
    for (const auto& [lbl, val] : fields()) {
      if (!summary.empty()) summary += L"; ";
      summary += lbl + L": " + val;
    }
    sub.name = summary;
    sub.local_bounds = card_rect_;
    return sub;
  }
  sub.role = A11yRole::Button;
  sub.can_invoke = enabled();
  sub.name = tr("connections.components.actions.closeConnection");
  sub.local_bounds = btn_close_conn_rect_;
  return sub;
}

bool ConnectionDetailDialog::a11y_invoke_virtual_child(size_t idx) {
  if (!open_ || !enabled() || idx == 0) return false;
  const bool can_close_conn = !closed_ && !item_.is_synthetic;
  if (!can_close_conn) return false;
  if (on_close_connection) on_close_connection(item_.id);
  close();
  return true;
}

}  // namespace clashwin::ui::scene

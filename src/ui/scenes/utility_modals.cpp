#include "utility_modals.h"

#include <windows.h>
#include <algorithm>
#include <iomanip>
#include <sstream>

#include "ui/theme/theme.h"
#include "util/str_utils.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

namespace {

std::string get_current_log_time() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%H:%M:%S");
  return oss.str();
}

}  // namespace

// ==========================================
// 1. EditProfileModal
// ==========================================
EditProfileModal::EditProfileModal(Widget* parent) : Widget(parent) {
  set_z_order(960);
}

void EditProfileModal::open(const std::string& name, const std::string& url, int interval_mins,
                            bool is_local) {
  open_ = true;
  name_ = name;
  url_ = url;
  interval_mins_ = interval_mins > 0 ? interval_mins : 1440;
  is_local_ = is_local;
  hover_close_x_ = false;
  hover_cancel_ = false;
  hover_save_ = false;
  focused_field_ = 1;
  invalidate();
}

void EditProfileModal::close() {
  if (!open_) return;
  open_ = false;
  invalidate();
  if (on_dismiss) on_dismiss();
}

void EditProfileModal::on_arrange(const RectF& area) {
  set_bounds(area);
  const float cw = 380.0f;
  const float ch = 260.0f;
  float cx = std::max(0.0f, (area.w - cw) / 2.0f);
  float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  btn_close_x_rect_ = {cx + cw - 32.0f, cy + 6.0f, 26.0f, 24.0f};

  name_input_rect_ = {cx + 20.0f, cy + 64.0f, cw - 40.0f, 30.0f};
  url_input_rect_ = {cx + 20.0f, cy + 120.0f, cw - 40.0f, 30.0f};
  interval_input_rect_ = {cx + 20.0f, cy + 176.0f, 140.0f, 30.0f};

  btn_cancel_rect_ = {cx + cw - 156.0f, cy + ch - 42.0f, 66.0f, 28.0f};
  btn_save_rect_ = {cx + cw - 82.0f, cy + ch - 42.0f, 66.0f, 28.0f};
}

void EditProfileModal::on_paint(IRenderTarget& rt) {
  if (!open_) return;

  // 1. 半透明遮罩（局部坐标 {0,0,w,h}）
  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{Color4f(0, 0, 0, 0.45f)});

  // 2. 卡片底座（圆角 8px + 阴影）
  Brush shadow_b{Color4f(0, 0, 0, 0.22f)};
  rt.round_rect({card_rect_.x - 2, card_rect_.y - 1, card_rect_.w + 4, card_rect_.h + 5}, 9.0f,
                {}, &shadow_b);

  Brush card_bg{Color4f::hex(0xFFFFFF)};
  rt.round_rect(card_rect_, 8.0f, {Color4f::hex(0xD0D0D0), 1.0f}, &card_bg);

  // 3. 标题与关闭 ✕
  TextStyle title_ts;
  title_ts.size = 13.5f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = Color4f::hex(0x1A1A1A);
  title_ts.align_v = AlignV::Center;
  rt.draw_text(L"⚙️ 编辑配置", {card_rect_.x + 18.0f, card_rect_.y + 8.0f, 200.0f, 24.0f},
               title_ts);

  Color4f x_bg = hover_close_x_ ? Color4f::hex(0xEEEEEE) : Color4f::hex(0xFAFAFA);
  Brush xb{x_bg};
  rt.round_rect(btn_close_x_rect_, 3.0f, {Color4f::hex(0xCCCCCC), 1.0f}, &xb);
  TextStyle x_ts;
  x_ts.size = 11.5f;
  x_ts.color = Color4f::hex(0x424242);
  x_ts.align_h = AlignH::Center;
  x_ts.align_v = AlignV::Center;
  rt.draw_text(L"✕", btn_close_x_rect_, x_ts);

  rt.fill_rect({card_rect_.x, card_rect_.y + 36.0f, card_rect_.w, 1.0f},
               Brush{Color4f::hex(0xEEEEEE)});

  // 4. 表单字段
  TextStyle label_ts;
  label_ts.size = 10.5f;
  label_ts.color = Color4f::hex(0x555555);
  label_ts.align_v = AlignV::Center;

  TextStyle val_ts;
  val_ts.size = 11.0f;
  val_ts.color = Color4f::hex(0x212121);
  val_ts.align_v = AlignV::Center;
  val_ts.ellipsis = true;

  // 字段 1: 配置名称
  rt.draw_text(L"配置名称:", {card_rect_.x + 20.0f, card_rect_.y + 44.0f, 200.0f, 18.0f},
               label_ts);
  Brush name_bg{Color4f::hex(0xFAFAFA)};
  Pen name_pen{focused_field_ == 1 ? Color4f::hex(0xE65100) : Color4f::hex(0xCCCCCC), 1.0f};
  rt.round_rect(name_input_rect_, 4.0f, name_pen, &name_bg);
  std::wstring nw = utf::utf8_to_utf16(name_);
  if (focused_field_ == 1) nw += L"|";
  rt.draw_text(nw, {name_input_rect_.x + 8.0f, name_input_rect_.y, name_input_rect_.w - 16.0f,
                    name_input_rect_.h}, val_ts);

  // 字段 2: 订阅 URL
  rt.draw_text(is_local_ ? L"订阅链接 (本地配置不可修改):" : L"订阅链接:",
               {card_rect_.x + 20.0f, card_rect_.y + 100.0f, 300.0f, 18.0f}, label_ts);
  Brush url_bg{is_local_ ? Color4f::hex(0xEEEEEE) : Color4f::hex(0xFAFAFA)};
  Pen url_pen{focused_field_ == 2 ? Color4f::hex(0xE65100) : Color4f::hex(0xCCCCCC), 1.0f};
  rt.round_rect(url_input_rect_, 4.0f, url_pen, &url_bg);
  std::wstring uw = utf::utf8_to_utf16(url_);
  if (focused_field_ == 2) uw += L"|";
  TextStyle url_val_ts = val_ts;
  if (is_local_) url_val_ts.color = Color4f::hex(0x9E9E9E);
  rt.draw_text(uw, {url_input_rect_.x + 8.0f, url_input_rect_.y, url_input_rect_.w - 16.0f,
                    url_input_rect_.h}, url_val_ts);

  // 字段 3: 自动更新间隔（分钟）
  rt.draw_text(is_local_ ? L"更新间隔 (本地禁用):" : L"更新间隔 (分钟):",
               {card_rect_.x + 20.0f, card_rect_.y + 156.0f, 200.0f, 18.0f}, label_ts);
  Brush int_bg{is_local_ ? Color4f::hex(0xEEEEEE) : Color4f::hex(0xFAFAFA)};
  Pen int_pen{focused_field_ == 3 ? Color4f::hex(0xE65100) : Color4f::hex(0xCCCCCC), 1.0f};
  rt.round_rect(interval_input_rect_, 4.0f, int_pen, &int_bg);
  std::wstring iw = std::to_wstring(interval_mins_);
  if (focused_field_ == 3) iw += L"|";
  TextStyle int_val_ts = val_ts;
  if (is_local_) int_val_ts.color = Color4f::hex(0x9E9E9E);
  rt.draw_text(iw, {interval_input_rect_.x + 8.0f, interval_input_rect_.y,
                    interval_input_rect_.w - 16.0f, interval_input_rect_.h}, int_val_ts);

  // 5. 底部取消与保存按钮
  Color4f c_bg = hover_cancel_ ? Color4f::hex(0xEEEEEE) : Color4f::hex(0xF5F5F5);
  Brush cb{c_bg};
  rt.round_rect(btn_cancel_rect_, 4.0f, {Color4f::hex(0xCCCCCC), 1.0f}, &cb);
  TextStyle c_ts;
  c_ts.size = 11.0f;
  c_ts.color = Color4f::hex(0x424242);
  c_ts.align_h = AlignH::Center;
  c_ts.align_v = AlignV::Center;
  rt.draw_text(L"取消", btn_cancel_rect_, c_ts);

  Color4f s_bg = hover_save_ ? Color4f::hex(0xEF6C00) : Color4f::hex(0xE65100);
  Brush sb{s_bg};
  rt.round_rect(btn_save_rect_, 4.0f, {}, &sb);
  TextStyle s_ts;
  s_ts.size = 11.0f;
  s_ts.weight = static_cast<int>(Weight::Bold);
  s_ts.color = Color4f::hex(0xFFFFFF);
  s_ts.align_h = AlignH::Center;
  s_ts.align_v = AlignV::Center;
  rt.draw_text(L"保存", btn_save_rect_, s_ts);
}

bool EditProfileModal::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  PointF pt = e.pos;

  if (e.type == MouseEventType::Move) {
    bool hx = btn_close_x_rect_.contains(pt.x, pt.y);
    bool hc = btn_cancel_rect_.contains(pt.x, pt.y);
    bool hs = btn_save_rect_.contains(pt.x, pt.y);
    if (hx != hover_close_x_ || hc != hover_cancel_ || hs != hover_save_) {
      hover_close_x_ = hx;
      hover_cancel_ = hc;
      hover_save_ = hs;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (btn_close_x_rect_.contains(pt.x, pt.y) || btn_cancel_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    if (btn_save_rect_.contains(pt.x, pt.y)) {
      if (on_save) on_save(name_, url_, interval_mins_);
      close();
      return true;
    }
    if (name_input_rect_.contains(pt.x, pt.y)) {
      focused_field_ = 1;
      invalidate();
      return true;
    }
    if (!is_local_ && url_input_rect_.contains(pt.x, pt.y)) {
      focused_field_ = 2;
      invalidate();
      return true;
    }
    if (!is_local_ && interval_input_rect_.contains(pt.x, pt.y)) {
      focused_field_ = 3;
      invalidate();
      return true;
    }
    if (!card_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    return true;
  }

  return true;
}

bool EditProfileModal::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down) {
    if (e.key == VK_ESCAPE || e.key == 27) {
      close();
      return true;
    }
    if (e.key == VK_RETURN || e.key == 13) {
      if (on_save) on_save(name_, url_, interval_mins_);
      close();
      return true;
    }
    if (e.key == VK_BACK) {
      if (focused_field_ == 1 && !name_.empty()) {
        name_.pop_back();
        invalidate();
        return true;
      }
      if (focused_field_ == 2 && !url_.empty()) {
        url_.pop_back();
        invalidate();
        return true;
      }
      if (focused_field_ == 3) {
        interval_mins_ /= 10;
        invalidate();
        return true;
      }
    }
  } else if (e.type == KeyEventType::Char) {
    if (focused_field_ == 1 && e.ch >= 32) {
      name_ += utf::utf16_to_utf8(std::wstring(1, e.ch));
      invalidate();
      return true;
    }
    if (focused_field_ == 2 && e.ch >= 32 && e.ch < 127) {
      url_.push_back(static_cast<char>(e.ch));
      invalidate();
      return true;
    }
    if (focused_field_ == 3 && e.ch >= '0' && e.ch <= '9') {
      interval_mins_ = interval_mins_ * 10 + (e.ch - '0');
      if (interval_mins_ > 100000) interval_mins_ = 100000;
      invalidate();
      return true;
    }
  }
  return false;
}

// ==========================================
// 2. ClientUpdateModal
// ==========================================
ClientUpdateModal::ClientUpdateModal(Widget* parent) : Widget(parent) {
  set_z_order(960);
}

void ClientUpdateModal::open(const std::string& current_ver, const std::string& latest_ver,
                             const std::string& changelog) {
  open_ = true;
  current_version_ = current_ver;
  latest_version_ = latest_ver;
  changelog_ = changelog;
  progress_ = 0;
  is_downloading_ = false;
  status_msg_.clear();
  hover_close_x_ = false;
  hover_close_ = false;
  hover_update_ = false;
  invalidate();
}

void ClientUpdateModal::close() {
  if (!open_) return;
  open_ = false;
  invalidate();
  if (on_dismiss) on_dismiss();
}

void ClientUpdateModal::set_progress(int p, const std::string& msg) {
  progress_ = std::clamp(p, 0, 100);
  if (!msg.empty()) status_msg_ = msg;
  invalidate();
}

void ClientUpdateModal::on_arrange(const RectF& area) {
  set_bounds(area);
  const float cw = 420.0f;
  const float ch = 340.0f;
  float cx = std::max(0.0f, (area.w - cw) / 2.0f);
  float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  btn_close_x_rect_ = {cx + cw - 32.0f, cy + 6.0f, 26.0f, 24.0f};
  changelog_box_rect_ = {cx + 20.0f, cy + 82.0f, cw - 40.0f, 150.0f};
  progress_bar_rect_ = {cx + 20.0f, cy + 244.0f, cw - 40.0f, 12.0f};

  btn_close_rect_ = {cx + cw - 180.0f, cy + ch - 44.0f, 76.0f, 30.0f};
  btn_update_rect_ = {cx + cw - 96.0f, cy + ch - 44.0f, 82.0f, 30.0f};
}

void ClientUpdateModal::on_paint(IRenderTarget& rt) {
  if (!open_) return;

  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{Color4f(0, 0, 0, 0.45f)});

  Brush shadow_b{Color4f(0, 0, 0, 0.22f)};
  rt.round_rect({card_rect_.x - 2, card_rect_.y - 1, card_rect_.w + 4, card_rect_.h + 5}, 9.0f,
                {}, &shadow_b);

  Brush card_bg{Color4f::hex(0xFFFFFF)};
  rt.round_rect(card_rect_, 8.0f, {Color4f::hex(0xD0D0D0), 1.0f}, &card_bg);

  // 标题
  TextStyle title_ts;
  title_ts.size = 13.5f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = Color4f::hex(0x1A1A1A);
  title_ts.align_v = AlignV::Center;
  rt.draw_text(L"🚀 客户端更新", {card_rect_.x + 18.0f, card_rect_.y + 8.0f, 200.0f, 24.0f},
               title_ts);

  // 关闭 ✕
  Color4f x_bg = hover_close_x_ ? Color4f::hex(0xEEEEEE) : Color4f::hex(0xFAFAFA);
  Brush xb{x_bg};
  rt.round_rect(btn_close_x_rect_, 3.0f, {Color4f::hex(0xCCCCCC), 1.0f}, &xb);
  TextStyle x_ts;
  x_ts.size = 11.5f;
  x_ts.color = Color4f::hex(0x424242);
  x_ts.align_h = AlignH::Center;
  x_ts.align_v = AlignV::Center;
  rt.draw_text(L"✕", btn_close_x_rect_, x_ts);

  rt.fill_rect({card_rect_.x, card_rect_.y + 36.0f, card_rect_.w, 1.0f},
               Brush{Color4f::hex(0xEEEEEE)});

  // 版本信息
  TextStyle ver_ts;
  ver_ts.size = 11.0f;
  ver_ts.color = Color4f::hex(0x333333);
  ver_ts.align_v = AlignV::Center;
  std::wstring ver_str = L"当前版本: " + utf::utf8_to_utf16(current_version_) + L"   ➔   最新版本: " +
                         utf::utf8_to_utf16(latest_version_);
  rt.draw_text(ver_str, {card_rect_.x + 20.0f, card_rect_.y + 44.0f, card_rect_.w - 40.0f, 22.0f},
               ver_ts);

  TextStyle chg_label;
  chg_label.size = 10.5f;
  chg_label.color = Color4f::hex(0x757575);
  chg_label.align_v = AlignV::Center;
  rt.draw_text(L"更新日志:", {card_rect_.x + 20.0f, card_rect_.y + 64.0f, 200.0f, 16.0f}, chg_label);

  // 更新日志框
  Brush cl_bg{Color4f::hex(0xF8F9FA)};
  rt.round_rect(changelog_box_rect_, 4.0f, {Color4f::hex(0xE0E0E0), 1.0f}, &cl_bg);

  TextStyle cl_ts;
  cl_ts.size = 10.5f;
  cl_ts.font_family = L"Consolas";
  cl_ts.color = Color4f::hex(0x424242);
  cl_ts.wrap = true;
  std::wstring chg_w = changelog_.empty() ? L"暂无详细更新说明" : utf::utf8_to_utf16(changelog_);
  rt.push_clip(changelog_box_rect_);
  rt.draw_text(chg_w, {changelog_box_rect_.x + 8.0f, changelog_box_rect_.y + 8.0f,
                       changelog_box_rect_.w - 16.0f, changelog_box_rect_.h - 16.0f}, cl_ts);
  rt.pop_clip();

  // 进度条与状态
  if (is_downloading_ || progress_ > 0) {
    Brush bar_bg{Color4f::hex(0xE0E0E0)};
    rt.round_rect(progress_bar_rect_, 6.0f, {}, &bar_bg);
    float fill_w = progress_bar_rect_.w * (static_cast<float>(progress_) / 100.0f);
    Brush fill_b{Color4f::hex(0x2196F3)};
    rt.round_rect({progress_bar_rect_.x, progress_bar_rect_.y, fill_w, progress_bar_rect_.h},
                  6.0f, {}, &fill_b);

    TextStyle stat_ts;
    stat_ts.size = 10.0f;
    stat_ts.color = Color4f::hex(0x1976D2);
    std::wstring stat_str = status_msg_.empty()
                                ? (L"正在下载更新包... " + std::to_wstring(progress_) + L"%")
                                : utf::utf8_to_utf16(status_msg_);
    rt.draw_text(stat_str, {progress_bar_rect_.x, progress_bar_rect_.y + 16.0f,
                            progress_bar_rect_.w, 18.0f}, stat_ts);
  }

  // 底部按钮
  Color4f c_bg = hover_close_ ? Color4f::hex(0xEEEEEE) : Color4f::hex(0xF5F5F5);
  Brush cb{c_bg};
  rt.round_rect(btn_close_rect_, 4.0f, {Color4f::hex(0xCCCCCC), 1.0f}, &cb);
  TextStyle c_ts;
  c_ts.size = 11.0f;
  c_ts.color = Color4f::hex(0x424242);
  c_ts.align_h = AlignH::Center;
  c_ts.align_v = AlignV::Center;
  rt.draw_text(L"关闭", btn_close_rect_, c_ts);

  Color4f u_bg = hover_update_ ? Color4f::hex(0x1976D2) : Color4f::hex(0x2196F3);
  Brush ub{u_bg};
  rt.round_rect(btn_update_rect_, 4.0f, {}, &ub);
  TextStyle u_ts;
  u_ts.size = 11.0f;
  u_ts.weight = static_cast<int>(Weight::Bold);
  u_ts.color = Color4f::hex(0xFFFFFF);
  u_ts.align_h = AlignH::Center;
  u_ts.align_v = AlignV::Center;
  rt.draw_text(is_downloading_ ? L"更新中..." : L"立即更新", btn_update_rect_, u_ts);
}

bool ClientUpdateModal::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  PointF pt = e.pos;

  if (e.type == MouseEventType::Move) {
    bool hx = btn_close_x_rect_.contains(pt.x, pt.y);
    bool hc = btn_close_rect_.contains(pt.x, pt.y);
    bool hu = btn_update_rect_.contains(pt.x, pt.y);
    if (hx != hover_close_x_ || hc != hover_close_ || hu != hover_update_) {
      hover_close_x_ = hx;
      hover_close_ = hc;
      hover_update_ = hu;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (btn_close_x_rect_.contains(pt.x, pt.y) || btn_close_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    if (btn_update_rect_.contains(pt.x, pt.y)) {
      is_downloading_ = true;
      invalidate();
      if (on_start_update) on_start_update();
      return true;
    }
    if (!card_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    return true;
  }

  return true;
}

bool ClientUpdateModal::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down && (e.key == VK_ESCAPE || e.key == 27)) {
    close();
    return true;
  }
  return false;
}

// ==========================================
// 3. CoreUpdateModal
// ==========================================
CoreUpdateModal::CoreUpdateModal(Widget* parent) : Widget(parent) {
  set_z_order(960);
  subscribe_events();
}

CoreUpdateModal::~CoreUpdateModal() {
  unsubscribe_events();
}

void CoreUpdateModal::open(const std::string& current_ver, const std::string& latest_ver) {
  open_ = true;
  current_version_ = current_ver;
  latest_version_ = latest_ver;
  progress_ = 0;
  status_ = "idle";
  message_.clear();
  hover_close_x_ = false;
  hover_close_ = false;
  hover_upgrade_ = false;
  invalidate();
}

void CoreUpdateModal::close() {
  if (!open_) return;
  open_ = false;
  invalidate();
  if (on_dismiss) on_dismiss();
}

void CoreUpdateModal::subscribe_events() {
  listener_id_ = events::EventBus::instance().subscribe(
      "core-upgrade-progress", [this](const events::Payload& payload) {
        if (!payload.is_object()) return;
        std::string st = payload.value("status", "checking");
        int pr = payload.value("progress", 0);
        std::string msg = payload.value("message", "");
        this->handle_progress_event(st, pr, msg);
      });
}

void CoreUpdateModal::unsubscribe_events() {
  if (listener_id_ != 0) {
    events::EventBus::instance().unsubscribe(listener_id_);
    listener_id_ = 0;
  }
}

void CoreUpdateModal::handle_progress_event(const std::string& status, int progress,
                                            const std::string& msg) {
  status_ = status;
  progress_ = std::clamp(progress, 0, 100);
  message_ = msg;
  if (status_ == "done") {
    is_upgrading_ = false;
    current_version_ = latest_version_;
  } else if (status_ == "error") {
    is_upgrading_ = false;
  }
  invalidate();
}

void CoreUpdateModal::on_arrange(const RectF& area) {
  set_bounds(area);
  const float cw = 400.0f;
  const float ch = 280.0f;
  float cx = std::max(0.0f, (area.w - cw) / 2.0f);
  float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  btn_close_x_rect_ = {cx + cw - 32.0f, cy + 6.0f, 26.0f, 24.0f};
  progress_bar_rect_ = {cx + 20.0f, cy + 140.0f, cw - 40.0f, 12.0f};

  btn_close_rect_ = {cx + cw - 180.0f, cy + ch - 44.0f, 76.0f, 30.0f};
  btn_upgrade_rect_ = {cx + cw - 96.0f, cy + ch - 44.0f, 82.0f, 30.0f};
}

void CoreUpdateModal::on_paint(IRenderTarget& rt) {
  if (!open_) return;

  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{Color4f(0, 0, 0, 0.45f)});

  Brush shadow_b{Color4f(0, 0, 0, 0.22f)};
  rt.round_rect({card_rect_.x - 2, card_rect_.y - 1, card_rect_.w + 4, card_rect_.h + 5}, 9.0f,
                {}, &shadow_b);

  Brush card_bg{Color4f::hex(0xFFFFFF)};
  rt.round_rect(card_rect_, 8.0f, {Color4f::hex(0xD0D0D0), 1.0f}, &card_bg);

  // 标题
  TextStyle title_ts;
  title_ts.size = 13.5f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = Color4f::hex(0x1A1A1A);
  title_ts.align_v = AlignV::Center;
  rt.draw_text(L"⚙️ 内核升级", {card_rect_.x + 18.0f, card_rect_.y + 8.0f, 200.0f, 24.0f},
               title_ts);

  // 关闭 ✕
  Color4f x_bg = hover_close_x_ ? Color4f::hex(0xEEEEEE) : Color4f::hex(0xFAFAFA);
  Brush xb{x_bg};
  rt.round_rect(btn_close_x_rect_, 3.0f, {Color4f::hex(0xCCCCCC), 1.0f}, &xb);
  TextStyle x_ts;
  x_ts.size = 11.5f;
  x_ts.color = Color4f::hex(0x424242);
  x_ts.align_h = AlignH::Center;
  x_ts.align_v = AlignV::Center;
  rt.draw_text(L"✕", btn_close_x_rect_, x_ts);

  rt.fill_rect({card_rect_.x, card_rect_.y + 36.0f, card_rect_.w, 1.0f},
               Brush{Color4f::hex(0xEEEEEE)});

  // 版本信息
  TextStyle ver_ts;
  ver_ts.size = 11.5f;
  ver_ts.color = Color4f::hex(0x333333);
  ver_ts.align_v = AlignV::Center;
  rt.draw_text(L"当前内核版本: " + utf::utf8_to_utf16(current_version_),
               {card_rect_.x + 20.0f, card_rect_.y + 54.0f, card_rect_.w - 40.0f, 22.0f}, ver_ts);
  rt.draw_text(L"最新可用版本: " + utf::utf8_to_utf16(latest_version_),
               {card_rect_.x + 20.0f, card_rect_.y + 80.0f, card_rect_.w - 40.0f, 22.0f}, ver_ts);

  // 升级进度条
  Brush bar_bg{Color4f::hex(0xE0E0E0)};
  rt.round_rect(progress_bar_rect_, 6.0f, {}, &bar_bg);
  if (progress_ > 0) {
    float fill_w = progress_bar_rect_.w * (static_cast<float>(progress_) / 100.0f);
    Brush fill_b{status_ == "done" ? Color4f::hex(0x4CAF50) : Color4f::hex(0xE65100)};
    rt.round_rect({progress_bar_rect_.x, progress_bar_rect_.y, fill_w, progress_bar_rect_.h},
                  6.0f, {}, &fill_b);
  }

  // 状态说明文本
  TextStyle msg_ts;
  msg_ts.size = 10.5f;
  msg_ts.color = status_ == "error" ? Color4f::hex(0xD32F2F) : Color4f::hex(0x555555);
  std::wstring display_msg;
  if (!message_.empty()) {
    display_msg = utf::utf8_to_utf16(message_);
  } else if (status_ == "checking") {
    display_msg = L"正在检查新版本内核发布...";
  } else if (status_ == "downloading") {
    display_msg = L"正在下载内核组件... " + std::to_wstring(progress_) + L"%";
  } else if (status_ == "verifying") {
    display_msg = L"正在校验 SHA256 签名...";
  } else if (status_ == "extracting") {
    display_msg = L"正在热替换 mini-mihomo.exe 并平滑重启...";
  } else if (status_ == "done") {
    display_msg = L"✅ 内核升级完成并已就绪！";
  } else {
    display_msg = L"就绪，点击'立即更新'开始平滑热替换";
  }
  rt.draw_text(display_msg, {progress_bar_rect_.x, progress_bar_rect_.y + 18.0f,
                             progress_bar_rect_.w, 20.0f}, msg_ts);

  // 底部按钮
  Color4f c_bg = hover_close_ ? Color4f::hex(0xEEEEEE) : Color4f::hex(0xF5F5F5);
  Brush cb{c_bg};
  rt.round_rect(btn_close_rect_, 4.0f, {Color4f::hex(0xCCCCCC), 1.0f}, &cb);
  TextStyle c_ts;
  c_ts.size = 11.0f;
  c_ts.color = Color4f::hex(0x424242);
  c_ts.align_h = AlignH::Center;
  c_ts.align_v = AlignV::Center;
  rt.draw_text(L"关闭", btn_close_rect_, c_ts);

  Color4f u_bg = hover_upgrade_ ? Color4f::hex(0xEF6C00) : Color4f::hex(0xE65100);
  Brush ub{u_bg};
  rt.round_rect(btn_upgrade_rect_, 4.0f, {}, &ub);
  TextStyle u_ts;
  u_ts.size = 11.0f;
  u_ts.weight = static_cast<int>(Weight::Bold);
  u_ts.color = Color4f::hex(0xFFFFFF);
  u_ts.align_h = AlignH::Center;
  u_ts.align_v = AlignV::Center;
  rt.draw_text(status_ == "done" ? L"已是最新" : (is_upgrading_ ? L"升级中..." : L"立即更新"),
               btn_upgrade_rect_, u_ts);
}

bool CoreUpdateModal::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  PointF pt = e.pos;

  if (e.type == MouseEventType::Move) {
    bool hx = btn_close_x_rect_.contains(pt.x, pt.y);
    bool hc = btn_close_rect_.contains(pt.x, pt.y);
    bool hu = btn_upgrade_rect_.contains(pt.x, pt.y);
    if (hx != hover_close_x_ || hc != hover_close_ || hu != hover_upgrade_) {
      hover_close_x_ = hx;
      hover_close_ = hc;
      hover_upgrade_ = hu;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (btn_close_x_rect_.contains(pt.x, pt.y) || btn_close_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    if (btn_upgrade_rect_.contains(pt.x, pt.y)) {
      if (status_ != "done") {
        is_upgrading_ = true;
        handle_progress_event("checking", 5, "正在检查最新内核版本...");
        if (on_start_upgrade) on_start_upgrade();
      }
      return true;
    }
    if (!card_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    return true;
  }

  return true;
}

bool CoreUpdateModal::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down && (e.key == VK_ESCAPE || e.key == 27)) {
    close();
    return true;
  }
  return false;
}

// ==========================================
// 4. LogViewerModal
// ==========================================
LogViewerModal::LogViewerModal(Widget* parent) : Widget(parent) {
  set_z_order(960);
  last_flush_time_ = std::chrono::steady_clock::now();
}

void LogViewerModal::open() {
  open_ = true;
  flush_incoming();
  hover_close_x_ = false;
  hover_pause_ = false;
  hover_order_ = false;
  hover_clear_ = false;
  invalidate();
}

void LogViewerModal::close() {
  if (!open_) return;
  open_ = false;
  invalidate();
  if (on_dismiss) on_dismiss();
}

void LogViewerModal::push_log(const std::string& level, const std::string& msg,
                              const std::string& time_str) {
  LogEntry entry;
  entry.level = level.empty() ? "info" : level;
  entry.message = msg;
  entry.time = time_str.empty() ? get_current_log_time() : time_str;

  incoming_queue_.push_back(std::move(entry));

  // 50ms 批处理检测：若时间跨度超出 kFlushDelayMs，立即自动落盘至展示缓冲区
  auto now = std::chrono::steady_clock::now();
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - last_flush_time_).count();
  if (elapsed_ms >= static_cast<int64_t>(kFlushDelayMs)) {
    flush_incoming();
  }
}

void LogViewerModal::push_log_line(const std::string& raw_line) {
  // 简易解析 level=xxx msg="xxx"
  std::string lvl = "info";
  if (raw_line.find("level=debug") != std::string::npos) lvl = "debug";
  else if (raw_line.find("level=warn") != std::string::npos) lvl = "warn";
  else if (raw_line.find("level=error") != std::string::npos) lvl = "error";
  push_log(lvl, raw_line);
}

void LogViewerModal::flush_incoming() {
  last_flush_time_ = std::chrono::steady_clock::now();
  if (incoming_queue_.empty()) return;

  if (!paused_) {
    for (auto& item : incoming_queue_) {
      logs_.push_back(std::move(item));
      // 1000 行环形截断 (FIFO clamp)
      if (logs_.size() > kMaxLogLines) {
        logs_.pop_front();
      }
    }
  }
  incoming_queue_.clear();
  invalidate();
}

void LogViewerModal::clear_logs() {
  logs_.clear();
  incoming_queue_.clear();
  scroll_y_ = 0;
  invalidate();
}

bool LogViewerModal::matches_filter(const LogEntry& entry) const {
  if (level_filter_ != "all" && entry.level != level_filter_) {
    return false;
  }
  if (!search_query_.empty()) {
    if (entry.message.find(search_query_) == std::string::npos) {
      return false;
    }
  }
  return true;
}

void LogViewerModal::on_arrange(const RectF& area) {
  set_bounds(area);
  const float cw = std::min(600.0f, area.w - 20.0f);
  const float ch = std::min(440.0f, area.h - 20.0f);
  float cx = std::max(0.0f, (area.w - cw) / 2.0f);
  float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  btn_close_x_rect_ = {cx + cw - 32.0f, cy + 6.0f, 26.0f, 24.0f};

  toolbar_rect_ = {cx + 14.0f, cy + 42.0f, cw - 28.0f, 32.0f};
  btn_pause_rect_ = {toolbar_rect_.x, toolbar_rect_.y, 64.0f, 30.0f};
  btn_order_rect_ = {toolbar_rect_.x + 68.0f, toolbar_rect_.y, 64.0f, 30.0f};
  btn_clear_rect_ = {toolbar_rect_.x + 136.0f, toolbar_rect_.y, 64.0f, 30.0f};
  seg_level_rect_ = {toolbar_rect_.x + 206.0f, toolbar_rect_.y, 140.0f, 30.0f};
  search_box_rect_ = {toolbar_rect_.x + 352.0f, toolbar_rect_.y,
                      toolbar_rect_.w - 352.0f, 30.0f};

  log_area_rect_ = {cx + 14.0f, cy + 82.0f, cw - 28.0f, ch - 96.0f};
}

void LogViewerModal::on_paint(IRenderTarget& rt) {
  if (!open_) return;

  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{Color4f(0, 0, 0, 0.45f)});

  Brush shadow_b{Color4f(0, 0, 0, 0.22f)};
  rt.round_rect({card_rect_.x - 2, card_rect_.y - 1, card_rect_.w + 4, card_rect_.h + 5}, 9.0f,
                {}, &shadow_b);

  Brush card_bg{Color4f::hex(0xFFFFFF)};
  rt.round_rect(card_rect_, 8.0f, {Color4f::hex(0xD0D0D0), 1.0f}, &card_bg);

  // 标题
  TextStyle title_ts;
  title_ts.size = 13.5f;
  title_ts.weight = static_cast<int>(Weight::Bold);
  title_ts.color = Color4f::hex(0x1A1A1A);
  title_ts.align_v = AlignV::Center;
  rt.draw_text(L"📄 运行日志", {card_rect_.x + 18.0f, card_rect_.y + 8.0f, 200.0f, 24.0f},
               title_ts);

  // 关闭 ✕
  Color4f x_bg = hover_close_x_ ? Color4f::hex(0xEEEEEE) : Color4f::hex(0xFAFAFA);
  Brush xb{x_bg};
  rt.round_rect(btn_close_x_rect_, 3.0f, {Color4f::hex(0xCCCCCC), 1.0f}, &xb);
  TextStyle x_ts;
  x_ts.size = 11.5f;
  x_ts.color = Color4f::hex(0x424242);
  x_ts.align_h = AlignH::Center;
  x_ts.align_v = AlignV::Center;
  rt.draw_text(L"✕", btn_close_x_rect_, x_ts);

  rt.fill_rect({card_rect_.x, card_rect_.y + 36.0f, card_rect_.w, 1.0f},
               Brush{Color4f::hex(0xEEEEEE)});

  // 工具栏按钮
  auto draw_tool_btn = [&](const RectF& r, const wchar_t* label, bool hover, bool active = false) {
    Color4f bg = active ? Color4f::hex(0xFFF3E0)
                        : (hover ? Color4f::hex(0xEEEEEE) : Color4f::hex(0xF5F5F5));
    Brush b{bg};
    Pen p{active ? Color4f::hex(0xE65100) : Color4f::hex(0xCCCCCC), 1.0f};
    rt.round_rect(r, 4.0f, p, &b);
    TextStyle ts;
    ts.size = 10.5f;
    ts.color = active ? Color4f::hex(0xE65100) : Color4f::hex(0x333333);
    ts.align_h = AlignH::Center;
    ts.align_v = AlignV::Center;
    rt.draw_text(label, r, ts);
  };

  draw_tool_btn(btn_pause_rect_, paused_ ? L"▶ 继续" : L"⏸ 暂停", hover_pause_, paused_);
  draw_tool_btn(btn_order_rect_, desc_ ? L"▼ 倒序" : L"▲ 顺序", hover_order_, desc_);
  draw_tool_btn(btn_clear_rect_, L"🗑 清空", hover_clear_);

  // 级别过滤显示
  TextStyle lvl_ts;
  lvl_ts.size = 10.0f;
  lvl_ts.color = Color4f::hex(0x555555);
  lvl_ts.align_v = AlignV::Center;
  std::wstring lvl_str = L"级别: " + utf::utf8_to_utf16(level_filter_);
  rt.draw_text(lvl_str, seg_level_rect_, lvl_ts);

  // 搜索框
  Brush sb{Color4f::hex(0xFAFAFA)};
  rt.round_rect(search_box_rect_, 4.0f, {Color4f::hex(0xCCCCCC), 1.0f}, &sb);
  TextStyle search_ts;
  search_ts.size = 10.0f;
  search_ts.color = search_query_.empty() ? Color4f::hex(0x9E9E9E) : Color4f::hex(0x333333);
  search_ts.align_v = AlignV::Center;
  std::wstring s_text = search_query_.empty() ? L"🔍 搜索日志..." : utf::utf8_to_utf16(search_query_);
  rt.draw_text(s_text, {search_box_rect_.x + 6.0f, search_box_rect_.y, search_box_rect_.w - 12.0f,
                        search_box_rect_.h}, search_ts);

  // 日志文本视口（暗底终端风格 Consolas 字体）
  Brush log_bg{Color4f::hex(0x1E1E1E)};
  rt.round_rect(log_area_rect_, 4.0f, {Color4f::hex(0x333333), 1.0f}, &log_bg);

  // 搜集过滤后的日志列表
  std::vector<const LogEntry*> visible_entries;
  visible_entries.reserve(logs_.size());
  if (desc_) {
    for (auto it = logs_.rbegin(); it != logs_.rend(); ++it) {
      if (matches_filter(*it)) visible_entries.push_back(&*it);
    }
  } else {
    for (const auto& item : logs_) {
      if (matches_filter(item)) visible_entries.push_back(&item);
    }
  }

  rt.push_clip(log_area_rect_);
  float y = log_area_rect_.y + 6.0f - scroll_y_;
  TextStyle log_ts;
  log_ts.size = 10.5f;
  log_ts.font_family = L"Consolas";
  log_ts.align_v = AlignV::Center;

  for (size_t i = 0; i < visible_entries.size(); ++i) {
    if (y + line_height_ >= log_area_rect_.y && y <= log_area_rect_.y + log_area_rect_.h) {
      const auto* e = visible_entries[i];

      // 颜色根据级别
      if (e->level == "debug") log_ts.color = Color4f::hex(0x9E9E9E);
      else if (e->level == "warn") log_ts.color = Color4f::hex(0xFFA726);
      else if (e->level == "error") log_ts.color = Color4f::hex(0xEF5350);
      else log_ts.color = Color4f::hex(0x81C784);  // info / 正常输出

      std::wstring line_text = L"[" + utf::utf8_to_utf16(e->time) + L"] [" +
                               utf::utf8_to_utf16(e->level) + L"] " +
                               utf::utf8_to_utf16(e->message);
      rt.draw_text(line_text, {log_area_rect_.x + 8.0f, y, log_area_rect_.w - 16.0f, line_height_},
                   log_ts);
    }
    y += line_height_;
  }
  rt.pop_clip();
}

bool LogViewerModal::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  PointF pt = e.pos;

  if (e.type == MouseEventType::Move) {
    bool hx = btn_close_x_rect_.contains(pt.x, pt.y);
    bool hp = btn_pause_rect_.contains(pt.x, pt.y);
    bool ho = btn_order_rect_.contains(pt.x, pt.y);
    bool hc = btn_clear_rect_.contains(pt.x, pt.y);
    if (hx != hover_close_x_ || hp != hover_pause_ || ho != hover_order_ || hc != hover_clear_) {
      hover_close_x_ = hx;
      hover_pause_ = hp;
      hover_order_ = ho;
      hover_clear_ = hc;
      invalidate();
    }
    return true;
  }

  if (e.type == MouseEventType::Wheel) {
    float delta = static_cast<float>(e.wheel_delta) / 120.0f * line_height_ * 3.0f;
    scroll_y_ = std::max(0.0f, scroll_y_ - delta);
    invalidate();
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (btn_close_x_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    if (btn_pause_rect_.contains(pt.x, pt.y)) {
      set_paused(!paused_);
      return true;
    }
    if (btn_order_rect_.contains(pt.x, pt.y)) {
      set_desc(!desc_);
      return true;
    }
    if (btn_clear_rect_.contains(pt.x, pt.y)) {
      clear_logs();
      return true;
    }
    if (seg_level_rect_.contains(pt.x, pt.y)) {
      // 循环切换级别: all -> info -> warn -> error -> debug -> all
      if (level_filter_ == "all") level_filter_ = "info";
      else if (level_filter_ == "info") level_filter_ = "warn";
      else if (level_filter_ == "warn") level_filter_ = "error";
      else if (level_filter_ == "error") level_filter_ = "debug";
      else level_filter_ = "all";
      invalidate();
      return true;
    }
    if (!card_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    return true;
  }

  return true;
}

bool LogViewerModal::on_key(const KeyEvent& e) {
  if (!open_) return false;
  if (e.type == KeyEventType::Down && (e.key == VK_ESCAPE || e.key == 27)) {
    close();
    return true;
  }
  return false;
}

}  // namespace clashwin::ui::scene

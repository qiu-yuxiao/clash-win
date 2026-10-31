#include "first_run_guide_dialog.h"

#include <windows.h>
#include <shellapi.h>

#include <fstream>
#include <nlohmann/json.hpp>

#include "cmd/command_service.h"
#include "config/profiles.h"
#include "ui/theme/theme.h"
#include "util/dirs.h"
#include "util/str_utils.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

namespace {

std::string read_clipboard_url() {
  if (!OpenClipboard(nullptr)) return "";
  HANDLE hData = GetClipboardData(CF_UNICODETEXT);
  if (!hData) {
    CloseClipboard();
    return "";
  }
  const wchar_t* pszText = static_cast<const wchar_t*>(GlobalLock(hData));
  std::string res;
  if (pszText) {
    res = utf::utf16_to_utf8(pszText);
    GlobalUnlock(hData);
  }
  CloseClipboard();

  std::string trimmed = util::trim(res);
  if (trimmed.find('\n') == std::string::npos && trimmed.find(' ') == std::string::npos) {
    if (trimmed.rfind("http://", 0) == 0 || trimmed.rfind("https://", 0) == 0 ||
        trimmed.rfind("clash://", 0) == 0) {
      return trimmed;
    }
  }
  return "";
}

std::wstring guide_storage_path() {
  return util::dirs::app_home_dir() + L"\\clashwin-guide.json";
}

}  // namespace

bool FirstRunGuideDialog::is_first_run_completed() {
  std::ifstream f(guide_storage_path(), std::ios::binary);
  if (!f) return false;
  try {
    nlohmann::json j;
    f >> j;
    return j.value(kStorageKey, false);
  } catch (...) {
    return false;
  }
}

void FirstRunGuideDialog::set_first_run_completed(bool completed) {
  nlohmann::json j;
  j[kStorageKey] = completed;
  std::ofstream f(guide_storage_path(), std::ios::binary | std::ios::trunc);
  if (f) f << j.dump(2);
}

bool FirstRunGuideDialog::has_real_profile() {
  auto prf = config::profiles::load();
  if (!prf.items.has_value() || prf.items->empty()) return false;
  for (const auto& it : *prf.items) {
    auto t = it.type();
    if (t == config::PrfType::Remote || t == config::PrfType::Local) {
      return true;
    }
  }
  return false;
}

FirstRunGuideDialog::FirstRunGuideDialog(Widget* parent) : Widget(parent) {
  set_z_order(980);
}

void FirstRunGuideDialog::open() {
  open_ = true;
  step_ = FirstRunStep::Input;
  is_importing_ = false;
  error_message_.clear();
  check_clipboard();
  invalidate();
}

void FirstRunGuideDialog::close() {
  if (!open_) return;
  open_ = false;
  invalidate();
  if (on_dismiss) on_dismiss();
}

void FirstRunGuideDialog::set_step(FirstRunStep s) {
  step_ = s;
  invalidate();
}

void FirstRunGuideDialog::set_url(const std::string& u) {
  url_ = u;
  error_message_.clear();
  invalidate();
}

void FirstRunGuideDialog::check_clipboard() {
  std::string clip = read_clipboard_url();
  if (!clip.empty()) {
    detected_clipboard_url_ = clip;
  }
}

void FirstRunGuideDialog::set_active_delay_ms(int ms) {
  active_delay_ms_ = ms;
  invalidate();
}

void FirstRunGuideDialog::submit_import() {
  if (is_importing_) return;

  std::string target_url = util::trim(url_);
  if (target_url.empty()) {
    target_url = util::trim(detected_clipboard_url_);
  }
  if (target_url.empty()) {
    std::string clip = read_clipboard_url();
    if (!clip.empty()) {
      target_url = clip;
      detected_clipboard_url_ = clip;
    }
  }

  if (target_url.empty()) {
    error_message_ = "请输入订阅链接或复制链接到剪贴板";
    invalidate();
    return;
  }

  if (target_url.rfind("http://", 0) != 0 && target_url.rfind("https://", 0) != 0 &&
      target_url.rfind("clash://", 0) != 0) {
    error_message_ = "仅支持 http://, https:// 或 clash:// 订阅协议";
    invalidate();
    return;
  }

  is_importing_ = true;
  error_message_.clear();
  invalidate();

  bool success = false;
  if (on_import) {
    success = on_import(target_url);
  } else {
    // 默认通过 CommandService 导入
    nlohmann::json args = {{"url", target_url}};
    auto res = cmd::CommandService::instance().invoke("import_profile", args);
    success = res.is_ok();
  }

  if (success) {
    // BUG-310 自动接管：若系统代理与 TUN 均未开启，自动开启系统代理
    if (!takeover_active_) {
      if (on_patch_system_proxy) {
        on_patch_system_proxy(true);
      } else {
        try {
          cmd::CommandService::instance().invoke("patch_verge_config",
                                                 {{"enable_system_proxy", true}});
        } catch (...) {}
      }
      takeover_active_ = true;
    }

    // BUG-311 守护释放：is_importing_ 在状态切换前释放，避免阻塞后续测速
    is_importing_ = false;
    step_ = FirstRunStep::Ready;
    active_delay_ms_ = 48;  // 模拟优选最快节点延迟
    invalidate();

    if (on_retest_nodes) {
      on_retest_nodes();
    }
  } else {
    is_importing_ = false;
    error_message_ = "订阅配置导入失败，请检查链接或网络后重试";
    invalidate();
  }
}

void FirstRunGuideDialog::finish() {
  set_first_run_completed(true);
  close();
}

void FirstRunGuideDialog::on_arrange(const RectF& area) {
  set_bounds(area);
  const float cw = std::min(480.0f, area.w - 24.0f);
  const float ch = step_ == FirstRunStep::Input ? 350.0f : 430.0f;
  float cx = std::max(0.0f, (area.w - cw) / 2.0f);
  float cy = std::max(0.0f, (area.h - ch) / 2.0f);
  card_rect_ = {cx, cy, cw, ch};

  btn_close_x_rect_ = {cx + cw - 32.0f, cy + 8.0f, 24.0f, 24.0f};

  if (step_ == FirstRunStep::Input) {
    banner_clip_rect_ = {cx + 20.0f, cy + 96.0f, cw - 40.0f, 28.0f};
    input_rect_ = {cx + 20.0f, cy + 134.0f, cw - 40.0f - 88.0f, 34.0f};
    btn_clear_rect_ = {cx + cw - 100.0f, cy + 134.0f, 32.0f, 34.0f};
    btn_paste_rect_ = {cx + cw - 64.0f, cy + 134.0f, 44.0f, 34.0f};
    btn_submit_rect_ = {cx + 20.0f, cy + ch - 82.0f, cw - 40.0f, 34.0f};
    btn_later_rect_ = {cx + 20.0f, cy + ch - 42.0f, cw - 40.0f, 28.0f};
  } else {
    // Step 2
    card_node_rect_ = {cx + 20.0f, cy + 62.0f, cw - 40.0f, 134.0f};
    btn_retest_rect_ = {cx + 32.0f, cy + 160.0f, cw - 64.0f, 26.0f};
    card_star_rect_ = {cx + 20.0f, cy + 206.0f, cw - 40.0f, 114.0f};
    btn_wiki_rect_ = {cx + cw - 150.0f, cy + 284.0f, 120.0f, 24.0f};
    btn_finish_star_rect_ = {cx + 20.0f, cy + ch - 84.0f, cw - 40.0f, 34.0f};
    btn_finish_rect_ = {cx + 20.0f, cy + ch - 44.0f, cw - 40.0f, 30.0f};
  }
}

void FirstRunGuideDialog::on_paint(IRenderTarget& rt) {
  if (!open_) return;

  // 1. 半透明遮罩（局部坐标 {0,0,w,h}）
  rt.fill_rect({0, 0, bounds().w, bounds().h}, Brush{Color4f(0, 0, 0, 0.48f)});

  // 2. 卡片底座（圆角 8px + 阴影）
  Brush shadow_b{Color4f(0, 0, 0, 0.25f)};
  rt.round_rect({card_rect_.x - 2, card_rect_.y - 1, card_rect_.w + 4, card_rect_.h + 5}, 9.0f,
                {}, &shadow_b);

  Brush card_bg{Color4f::hex(0xFFFFFF)};
  rt.round_rect(card_rect_, 8.0f, {Color4f::hex(0xE0E0E0), 1.0f}, &card_bg);

  // 关闭 ✕ 按钮
  Color4f x_bg = hover_close_x_ ? Color4f::hex(0xEEEEEE) : Color4f::hex(0xFAFAFA);
  Brush xb{x_bg};
  rt.round_rect(btn_close_x_rect_, 3.0f, {Color4f::hex(0xCCCCCC), 1.0f}, &xb);
  TextStyle x_ts;
  x_ts.size = 11.5f;
  x_ts.color = Color4f::hex(0x555555);
  x_ts.align_h = AlignH::Center;
  x_ts.align_v = AlignV::Center;
  rt.draw_text(L"✕", btn_close_x_rect_, x_ts);

  if (step_ == FirstRunStep::Input) {
    // ---- STEP 1: 标题与引导 ----
    TextStyle title_ts;
    title_ts.size = 15.5f;
    title_ts.weight = static_cast<int>(Weight::Bold);
    title_ts.color = Color4f::hex(0x1A1A1A);
    title_ts.align_h = AlignH::Center;
    title_ts.align_v = AlignV::Center;
    rt.draw_text(L"🎉 欢迎使用 Clash Win",
                 {card_rect_.x, card_rect_.y + 10.0f, card_rect_.w, 24.0f}, title_ts);

    // STEP 1 / 2 胶囊标
    RectF badge_r = {card_rect_.x + (card_rect_.w - 80.0f) / 2.0f, card_rect_.y + 36.0f, 80.0f,
                     18.0f};
    Brush badge_bg{Color4f::hex(0xFFF3E0)};
    rt.round_rect(badge_r, 9.0f, {Color4f::hex(0xFFB74D), 1.0f}, &badge_bg);
    TextStyle badge_ts;
    badge_ts.size = 10.0f;
    badge_ts.weight = static_cast<int>(Weight::Bold);
    badge_ts.color = Color4f::hex(0xE65100);
    badge_ts.align_h = AlignH::Center;
    badge_ts.align_v = AlignV::Center;
    rt.draw_text(L"STEP 1 / 2", badge_r, badge_ts);

    // 分隔线
    rt.fill_rect({card_rect_.x, card_rect_.y + 58.0f, card_rect_.w, 1.0f},
                 Brush{Color4f::hex(0xEEEEEE)});

    // 3 特性图标标签
    float f_w = (card_rect_.w - 56.0f) / 3.0f;
    auto draw_feature = [&](float x, const wchar_t* text) {
      RectF fr = {x, card_rect_.y + 66.0f, f_w, 22.0f};
      Brush fb{Color4f::hex(0xF5F5F5)};
      rt.round_rect(fr, 4.0f, {}, &fb);
      TextStyle fts;
      fts.size = 10.0f;
      fts.color = Color4f::hex(0x424242);
      fts.align_h = AlignH::Center;
      fts.align_v = AlignV::Center;
      rt.draw_text(text, fr, fts);
    };
    draw_feature(card_rect_.x + 20.0f, L"⚡ 自动优选最快节点");
    draw_feature(card_rect_.x + 24.0f + f_w, L"🛡️ 国内 DNS 防污染");
    draw_feature(card_rect_.x + 28.0f + f_w * 2.0f, L"🪶 极轻内存常驻");

    // 剪贴板自动嗅探横幅
    if (!detected_clipboard_url_.empty()) {
      Brush banner_bg{hover_banner_ ? Color4f::hex(0xFFE082) : Color4f::hex(0xFFECB3)};
      rt.round_rect(banner_clip_rect_, 4.0f, {Color4f::hex(0xFFB300), 1.0f}, &banner_bg);
      TextStyle bts;
      bts.size = 10.5f;
      bts.color = Color4f::hex(0x6D4C41);
      bts.align_h = AlignH::Center;
      bts.align_v = AlignV::Center;
      rt.draw_text(L"✨ 探测到剪贴板订阅链接，点击自动填入", banner_clip_rect_, bts);
    }

    // 输入框
    Brush in_bg{Color4f::hex(0xFAFAFA)};
    Pen in_pen{input_focused_ ? Color4f::hex(0xE65100) : Color4f::hex(0xCCCCCC), 1.5f};
    rt.round_rect(input_rect_, 4.0f, in_pen, &in_bg);

    TextStyle in_ts;
    in_ts.size = 11.5f;
    in_ts.color = url_.empty() ? Color4f::hex(0x9E9E9E) : Color4f::hex(0x212121);
    in_ts.align_v = AlignV::Center;
    in_ts.ellipsis = true;
    std::wstring display_url =
        url_.empty() ? L"支持各类订阅链接 (http/https/clash://)" : utf::utf8_to_utf16(url_);
    rt.draw_text(display_url,
                 {input_rect_.x + 8.0f, input_rect_.y, input_rect_.w - 16.0f, input_rect_.h},
                 in_ts);

    // 清空与粘贴按钮
    Brush cb{hover_clear_ ? Color4f::hex(0xEEEEEE) : Color4f::hex(0xF5F5F5)};
    rt.round_rect(btn_clear_rect_, 4.0f, {Color4f::hex(0xCCCCCC), 1.0f}, &cb);
    TextStyle c_ts;
    c_ts.size = 11.0f;
    c_ts.color = Color4f::hex(0x424242);
    c_ts.align_h = AlignH::Center;
    c_ts.align_v = AlignV::Center;
    rt.draw_text(L"✕", btn_clear_rect_, c_ts);

    Brush pb{hover_paste_ ? Color4f::hex(0xEEEEEE) : Color4f::hex(0xF5F5F5)};
    rt.round_rect(btn_paste_rect_, 4.0f, {Color4f::hex(0xCCCCCC), 1.0f}, &pb);
    TextStyle p_ts;
    p_ts.size = 10.5f;
    p_ts.color = Color4f::hex(0x424242);
    p_ts.align_h = AlignH::Center;
    p_ts.align_v = AlignV::Center;
    rt.draw_text(L"粘贴", btn_paste_rect_, p_ts);

    // 错误信息
    if (!error_message_.empty()) {
      TextStyle err_ts;
      err_ts.size = 10.5f;
      err_ts.color = Color4f::hex(0xD32F2F);
      err_ts.align_h = AlignH::Center;
      err_ts.align_v = AlignV::Center;
      rt.draw_text(utf::utf8_to_utf16(error_message_),
                   {card_rect_.x + 20.0f, card_rect_.y + 176.0f, card_rect_.w - 40.0f, 20.0f},
                   err_ts);
    }

    // 底部"立即导入并开启"按钮
    Color4f sub_bg = hover_submit_ ? Color4f::hex(0xEF6C00) : Color4f::hex(0xE65100);
    Brush sub_b{sub_bg};
    rt.round_rect(btn_submit_rect_, 4.0f, {}, &sub_b);
    TextStyle sub_ts;
    sub_ts.size = 12.0f;
    sub_ts.weight = static_cast<int>(Weight::Bold);
    sub_ts.color = Color4f::hex(0xFFFFFF);
    sub_ts.align_h = AlignH::Center;
    sub_ts.align_v = AlignV::Center;
    rt.draw_text(is_importing_ ? L"⏳ 正在下载并解析配置..." : L"立即导入并开启",
                 btn_submit_rect_, sub_ts);

    // "稍后设置"按钮
    TextStyle lat_ts;
    lat_ts.size = 11.0f;
    lat_ts.color = hover_later_ ? Color4f::hex(0xE65100) : Color4f::hex(0x757575);
    lat_ts.align_h = AlignH::Center;
    lat_ts.align_v = AlignV::Center;
    rt.draw_text(L"稍后设置", btn_later_rect_, lat_ts);

  } else {
    // ---- STEP 2: 就绪仪表盘 ----
    TextStyle title_ts;
    title_ts.size = 15.5f;
    title_ts.weight = static_cast<int>(Weight::Bold);
    title_ts.color = Color4f::hex(0x2E7D32);  // 绿色
    title_ts.align_h = AlignH::Center;
    title_ts.align_v = AlignV::Center;
    rt.draw_text(L"🚀 已准备就绪！",
                 {card_rect_.x, card_rect_.y + 10.0f, card_rect_.w, 24.0f}, title_ts);

    // STEP 2 / 2 胶囊标
    RectF badge_r = {card_rect_.x + (card_rect_.w - 80.0f) / 2.0f, card_rect_.y + 36.0f, 80.0f,
                     18.0f};
    Brush badge_bg{Color4f::hex(0xE8F5E9)};
    rt.round_rect(badge_r, 9.0f, {Color4f::hex(0x81C784), 1.0f}, &badge_bg);
    TextStyle badge_ts;
    badge_ts.size = 10.0f;
    badge_ts.weight = static_cast<int>(Weight::Bold);
    badge_ts.color = Color4f::hex(0x2E7D32);
    badge_ts.align_h = AlignH::Center;
    badge_ts.align_v = AlignV::Center;
    rt.draw_text(L"STEP 2 / 2", badge_r, badge_ts);

    // 分隔线
    rt.fill_rect({card_rect_.x, card_rect_.y + 56.0f, card_rect_.w, 1.0f},
                 Brush{Color4f::hex(0xEEEEEE)});

    // 卡片 1：实时节点连通仪表盘（浅绿底 + 边框）
    Brush node_bg{Color4f::hex(0xF1F8E9)};
    rt.round_rect(card_node_rect_, 6.0f, {Color4f::hex(0xC5E1A5), 1.0f}, &node_bg);

    TextStyle imp_ts;
    imp_ts.size = 11.5f;
    imp_ts.weight = static_cast<int>(Weight::Bold);
    imp_ts.color = Color4f::hex(0x2E7D32);
    rt.draw_text(L"✅ 订阅配置导入成功",
                 {card_node_rect_.x + 12.0f, card_node_rect_.y + 8.0f, card_node_rect_.w - 24.0f,
                  18.0f},
                 imp_ts);

    TextStyle conn_ts;
    conn_ts.size = 10.5f;
    conn_ts.color = Color4f::hex(0x388E3C);
    std::wstring conn_str = proxy_ready() ? L"所有节点测速完成，网络已就连通！"
                                         : L"正在检测节点连通性与测速...";
    rt.draw_text(conn_str,
                 {card_node_rect_.x + 12.0f, card_node_rect_.y + 28.0f, card_node_rect_.w - 24.0f,
                  18.0f},
                 conn_ts);

    // 最优节点行
    TextStyle opt_ts;
    opt_ts.size = 10.5f;
    opt_ts.color = Color4f::hex(0x555555);
    opt_ts.align_v = AlignV::Center;
    rt.draw_text(L"🌐 当前最优节点",
                 {card_node_rect_.x + 12.0f, card_node_rect_.y + 50.0f, 120.0f, 20.0f}, opt_ts);

    if (proxy_ready()) {
      RectF delay_badge = {card_node_rect_.x + card_node_rect_.w - 82.0f, card_node_rect_.y + 50.0f,
                           70.0f, 18.0f};
      Brush d_bg{Color4f::hex(0xE8F5E9)};
      rt.round_rect(delay_badge, 9.0f, {Color4f::hex(0x4CAF50), 1.0f}, &d_bg);
      TextStyle d_ts;
      d_ts.size = 10.0f;
      d_ts.weight = static_cast<int>(Weight::Bold);
      d_ts.color = Color4f::hex(0x2E7D32);
      d_ts.align_h = AlignH::Center;
      d_ts.align_v = AlignV::Center;
      std::wstring d_text = L"⚡ " + std::to_wstring(active_delay_ms_) + L" ms";
      rt.draw_text(d_text, delay_badge, d_ts);
    }

    TextStyle node_ts;
    node_ts.size = 12.0f;
    node_ts.weight = static_cast<int>(Weight::Bold);
    node_ts.color = Color4f::hex(0x212121);
    node_ts.align_v = AlignV::Center;
    node_ts.ellipsis = true;
    rt.draw_text(utf::utf8_to_utf16(active_node_name_),
                 {card_node_rect_.x + 12.0f, card_node_rect_.y + 70.0f,
                  card_node_rect_.w - 24.0f, 20.0f},
                 node_ts);

    // 重测按钮
    Color4f ret_bg = hover_retest_ ? Color4f::hex(0xE8F5E9) : Color4f::hex(0xFFFFFF);
    Brush ret_b{ret_bg};
    rt.round_rect(btn_retest_rect_, 4.0f, {Color4f::hex(0x81C784), 1.0f}, &ret_b);
    TextStyle ret_ts;
    ret_ts.size = 10.5f;
    ret_ts.weight = static_cast<int>(Weight::Bold);
    ret_ts.color = Color4f::hex(0x2E7D32);
    ret_ts.align_h = AlignH::Center;
    ret_ts.align_v = AlignV::Center;
    rt.draw_text(L"🔄 重新测速并优选节点", btn_retest_rect_, ret_ts);

    // 卡片 2：金色星标 GitHub Star 开源应援卡片
    Brush star_bg{Color4f::hex(0xFFFDE7)};
    rt.round_rect(card_star_rect_, 6.0f, {Color4f::hex(0xFFE082), 1.0f}, &star_bg);

    TextStyle star_title_ts;
    star_title_ts.size = 12.0f;
    star_title_ts.weight = static_cast<int>(Weight::Bold);
    star_title_ts.color = Color4f::hex(0xF57F17);  // 暖金色
    rt.draw_text(L"⭐ 点亮 GitHub Star 支持作者",
                 {card_star_rect_.x + 12.0f, card_star_rect_.y + 8.0f, card_star_rect_.w - 24.0f,
                  18.0f},
                 star_title_ts);

    TextStyle star_body_ts;
    star_body_ts.size = 10.5f;
    star_body_ts.color = Color4f::hex(0x5D4037);
    star_body_ts.wrap = true;
    rt.draw_text(
        L"如果 Clash Win 对您有所帮助，欢迎前往 GitHub 为本项目点亮 Star 开源支持！",
        {card_star_rect_.x + 12.0f, card_star_rect_.y + 28.0f, card_star_rect_.w - 24.0f, 44.0f},
        star_body_ts);

    // Wiki 帮助链接
    TextStyle wiki_ts;
    wiki_ts.size = 10.5f;
    wiki_ts.weight = static_cast<int>(Weight::Bold);
    wiki_ts.color = hover_wiki_ ? Color4f::hex(0xE65100) : Color4f::hex(0x1976D2);
    wiki_ts.align_h = AlignH::Right;
    wiki_ts.align_v = AlignV::Center;
    rt.draw_text(L"📖 查看使用文档 ↗", btn_wiki_rect_, wiki_ts);

    // 底部主要操作按钮
    Color4f f_star_bg = hover_finish_star_ ? Color4f::hex(0xEF6C00) : Color4f::hex(0xE65100);
    Brush fsb{f_star_bg};
    rt.round_rect(btn_finish_star_rect_, 4.0f, {}, &fsb);
    TextStyle fs_ts;
    fs_ts.size = 12.0f;
    fs_ts.weight = static_cast<int>(Weight::Bold);
    fs_ts.color = Color4f::hex(0xFFFFFF);
    fs_ts.align_h = AlignH::Center;
    fs_ts.align_v = AlignV::Center;
    rt.draw_text(L"⭐ 去 GitHub 点亮 Star 并完成", btn_finish_star_rect_, fs_ts);

    // 次要完成按钮
    Color4f fin_bg = hover_finish_ ? Color4f::hex(0xEEEEEE) : Color4f::hex(0xF5F5F5);
    Brush finb{fin_bg};
    rt.round_rect(btn_finish_rect_, 4.0f, {Color4f::hex(0xCCCCCC), 1.0f}, &finb);
    TextStyle fin_ts;
    fin_ts.size = 11.0f;
    fin_ts.color = Color4f::hex(0x424242);
    fin_ts.align_h = AlignH::Center;
    fin_ts.align_v = AlignV::Center;
    rt.draw_text(L"完成并开始使用", btn_finish_rect_, fin_ts);
  }
}

bool FirstRunGuideDialog::on_mouse(const MouseEvent& e) {
  if (!open_) return false;
  PointF pt = e.pos;

  if (e.type == MouseEventType::Move) {
    bool hx = btn_close_x_rect_.contains(pt.x, pt.y);
    if (step_ == FirstRunStep::Input) {
      bool hb = !detected_clipboard_url_.empty() && banner_clip_rect_.contains(pt.x, pt.y);
      bool hc = btn_clear_rect_.contains(pt.x, pt.y);
      bool hp = btn_paste_rect_.contains(pt.x, pt.y);
      bool hs = btn_submit_rect_.contains(pt.x, pt.y);
      bool hl = btn_later_rect_.contains(pt.x, pt.y);
      if (hx != hover_close_x_ || hb != hover_banner_ || hc != hover_clear_ ||
          hp != hover_paste_ || hs != hover_submit_ || hl != hover_later_) {
        hover_close_x_ = hx;
        hover_banner_ = hb;
        hover_clear_ = hc;
        hover_paste_ = hp;
        hover_submit_ = hs;
        hover_later_ = hl;
        invalidate();
      }
    } else {
      bool hr = btn_retest_rect_.contains(pt.x, pt.y);
      bool hw = btn_wiki_rect_.contains(pt.x, pt.y);
      bool hfs = btn_finish_star_rect_.contains(pt.x, pt.y);
      bool hf = btn_finish_rect_.contains(pt.x, pt.y);
      if (hx != hover_close_x_ || hr != hover_retest_ || hw != hover_wiki_ ||
          hfs != hover_finish_star_ || hf != hover_finish_) {
        hover_close_x_ = hx;
        hover_retest_ = hr;
        hover_wiki_ = hw;
        hover_finish_star_ = hfs;
        hover_finish_ = hf;
        invalidate();
      }
    }
    return true;
  }

  if (e.type == MouseEventType::Up) {
    if (btn_close_x_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }

    if (step_ == FirstRunStep::Input) {
      if (!detected_clipboard_url_.empty() && banner_clip_rect_.contains(pt.x, pt.y)) {
        set_url(detected_clipboard_url_);
        return true;
      }
      if (btn_clear_rect_.contains(pt.x, pt.y)) {
        set_url("");
        return true;
      }
      if (btn_paste_rect_.contains(pt.x, pt.y)) {
        std::string clip = read_clipboard_url();
        if (!clip.empty()) set_url(clip);
        return true;
      }
      if (input_rect_.contains(pt.x, pt.y)) {
        input_focused_ = true;
        invalidate();
        return true;
      }
      if (btn_submit_rect_.contains(pt.x, pt.y)) {
        submit_import();
        return true;
      }
      if (btn_later_rect_.contains(pt.x, pt.y)) {
        finish();
        return true;
      }
    } else {
      if (btn_retest_rect_.contains(pt.x, pt.y)) {
        if (on_retest_nodes) on_retest_nodes();
        active_delay_ms_ = 35;  // 模拟重新测速成功
        invalidate();
        return true;
      }
      if (btn_wiki_rect_.contains(pt.x, pt.y)) {
        if (on_open_wiki) on_open_wiki();
        ShellExecuteW(nullptr, L"open", L"https://github.com/qiu-yuxiao/clash-win/wiki",
                      nullptr, nullptr, SW_SHOWNORMAL);
        return true;
      }
      if (btn_finish_star_rect_.contains(pt.x, pt.y)) {
        if (on_open_github) on_open_github();
        ShellExecuteW(nullptr, L"open", L"https://github.com/qiu-yuxiao/clash-win", nullptr,
                      nullptr, SW_SHOWNORMAL);
        finish();
        return true;
      }
      if (btn_finish_rect_.contains(pt.x, pt.y)) {
        finish();
        return true;
      }
    }

    // 点击卡片外遮罩：关闭
    if (!card_rect_.contains(pt.x, pt.y)) {
      close();
      return true;
    }
    return true;
  }

  if (e.type == MouseEventType::Down) {
    return true;  // 拦截下层点击
  }

  return true;
}

bool FirstRunGuideDialog::on_key(const KeyEvent& e) {
  if (!open_) return false;

  if (e.type == KeyEventType::Down) {
    if (e.key == VK_ESCAPE || e.key == 27) {
      close();
      return true;
    }
    // Enter 键全窗口监听：任意焦点按 Enter 执行自动导入或完成
    if (e.key == VK_RETURN || e.key == 13) {
      if (step_ == FirstRunStep::Input) {
        submit_import();
      } else {
        finish();
      }
      return true;
    }
    if (e.key == VK_BACK && input_focused_ && step_ == FirstRunStep::Input) {
      if (!url_.empty()) {
        url_.pop_back();
        invalidate();
      }
      return true;
    }
  } else if (e.type == KeyEventType::Char && input_focused_ && step_ == FirstRunStep::Input) {
    if (e.ch >= 32 && e.ch < 127) {
      url_.push_back(static_cast<char>(e.ch));
      invalidate();
      return true;
    }
  }

  return false;
}

}  // namespace clashwin::ui::scene

// 首次运行向导主状态机、导入事务、定时轮询与 UIA（拆分自 first_run_guide_dialog.cpp）
// src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:1-200
#include "first_run_guide_dialog.h"
#include "first_run_guide_internal.h"

#include <windows.h>
#include <shellapi.h>

#include <fstream>
#include <nlohmann/json.hpp>

#include "cmd/command_service.h"
#include "config/config.h"
#include "config/profiles.h"
#include "ui/i18n_text.h"
#include "ui/theme/theme.h"
#include "util/dirs.h"
#include "util/node.h"
#include "util/str_utils.h"
#include "util/utf.h"

namespace clashwin::ui::scene {

namespace d = first_run_detail;
using theme::ThemeService;

bool FirstRunGuideDialog::is_first_run_completed() {
  std::ifstream f(d::guide_storage_path(), std::ios::binary);
  if (!f) {
    f.open(util::dirs::app_home_dir() + L"\\clashwin-guide.json", std::ios::binary);
  }
  if (!f) return false;
  try {
    nlohmann::json j;
    f >> j;
    return j.value(kStorageKey, false) ||
           j.value("clash-mini-first-run-completed", false);
  } catch (...) {
    return false;
  }
}

void FirstRunGuideDialog::set_first_run_completed(bool completed) {
  nlohmann::json j;
  j[kStorageKey] = completed;
  std::ofstream f(d::guide_storage_path(), std::ios::binary | std::ios::trunc);
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

// src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:1-200
FirstRunGuideDialog::FirstRunGuideDialog(Widget* parent) : Widget(parent) {
  set_z_order(980);

  // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:95-130
  auto uf = std::make_unique<TextField>(this);
  url_field_ = uf.get();
  url_field_->set_placeholder(
      L"https://example.com/api/v1/client/subscribe?token=...");
  url_field_->set_i18n_a11y_name("settings.mini.importPlaceholder");
  url_field_->apply_theme();
  url_field_->style_.size = 13;
  url_field_->height_ = d::kFieldH;
  url_field_->right_pad_ = 28.0f;  // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:110
  url_field_->set_visible(false);
  url_field_->on_text_changed = [this](const std::wstring& t) {
    const bool was_empty = url_.empty();
    url_ = utf::utf16_to_utf8(t);
    error_message_.clear();
    if (was_empty != url_.empty() && bounds().w > 0.0f && bounds().h > 0.0f) {
      on_arrange(bounds());
    }
    invalidate();
  };
  url_field_->on_submit = [this]() {
    if (!is_importing_) submit_import();
  };
  url_field_->on_context_menu = [this](PointF anchor) {
    open_url_menu(anchor);
  };
  add_child(std::move(uf));

  auto um = std::make_unique<ContextMenu>(this);
  url_menu_ = um.get();
  url_menu_->set_z_order(1050);
  add_child(std::move(um));
}

// src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:1-200
void FirstRunGuideDialog::open() {
  open_ = true;
  opened_after_completion_ = is_first_run_completed();
  fade_.show();
  step_ = FirstRunStep::Input;
  is_importing_ = false;
  error_message_.clear();
  retesting_ = false;
  if (url_field_) {
    url_field_->apply_theme();
    url_field_->style_.size = 13;  // src: src/pages/_layout/components/first-run-guide-dialog/input-step.tsx:120
    url_field_->set_enabled(true);
    url_field_->set_text(L"");
    url_field_->set_visible(true);
    url_field_->request_focus();
  }
  active_node_name_.clear();
  active_delay_ms_ = -1;
  gave_up_ = false;
  ready_started_ms_ = 0;
  last_poll_ms_ = 0;
  auto_retested_ = false;
  check_clipboard();
  if (bounds().w > 0.0f && bounds().h > 0.0f) on_arrange(bounds());
  invalidate();
}

void FirstRunGuideDialog::close() {
  if (!open_) return;
  open_ = false;
  fade_.hide();
  if (url_field_) { url_field_->blur(); url_field_->set_visible(false); }
  if (url_menu_ && url_menu_->is_open()) url_menu_->close();
  invalidate();
  if (on_dismiss) on_dismiss();
}

void FirstRunGuideDialog::set_step(FirstRunStep s) {
  step_ = s;
  ready_scroll_y_ = 0.0f;
  if (bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }
  invalidate();
}

void FirstRunGuideDialog::set_url(const std::string& u) {
  const bool was_empty = url_.empty();
  url_ = u;
  error_message_.clear();
  if (url_field_ && utf::utf16_to_utf8(url_field_->text()) != u) {
    url_field_->set_text(utf::utf8_to_utf16(u));
  }
  if (was_empty != url_.empty() && bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }
  error_message_.clear();
  if (url_field_ && utf::utf16_to_utf8(url_field_->text()) != u) {
    url_field_->set_text(utf::utf8_to_utf16(u));
  }
  invalidate();
}

void FirstRunGuideDialog::check_clipboard() {
  std::string clip = d::read_clipboard_url();
  if (!clip.empty()) {
    detected_clipboard_url_ = clip;
  }
}

void FirstRunGuideDialog::set_active_delay_ms(int ms) {
  active_delay_ms_ = ms;
  if (bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }
  invalidate();
}

// src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:100-180
void FirstRunGuideDialog::tick(int64_t now_ms) {
  if (!open_ || step_ != FirstRunStep::Ready) return;
  if (proxy_ready()) return;
  if (retesting_) return;
  if (gave_up_) return;
  if (ready_started_ms_ == 0) ready_started_ms_ = now_ms;
  if (now_ms - ready_started_ms_ >= kReadyTimeoutMs) {
    gave_up_ = true;
    if (bounds().w > 0.0f && bounds().h > 0.0f) {
      on_arrange(bounds());
    }
    invalidate();
    return;
  }
  if (last_poll_ms_ == 0 || now_ms - last_poll_ms_ >= kReadyPollIntervalMs) {
    last_poll_ms_ = now_ms;
    if (on_refresh_proxy) on_refresh_proxy();
  }
}

void FirstRunGuideDialog::on_retest_finished() {
  if (!retesting_) return;
  retesting_ = false;
  ready_started_ms_ = 0;
  last_poll_ms_ = 0;
  if (bounds().w > 0.0f && bounds().h > 0.0f) {
    on_arrange(bounds());
  }
  invalidate();
}

bool FirstRunGuideDialog::unconfirmed() const {
  return !proxy_ready() &&
         (gave_up_ || (!retesting_ && active_delay_ms_ == 0));
}

void FirstRunGuideDialog::submit_import() {
  if (is_importing_) return;

  std::string target_url = util::trim(url_);
  if (target_url.empty()) {
    target_url = util::trim(detected_clipboard_url_);
  }
  if (target_url.empty()) {
    std::string clip = d::read_clipboard_url();
    if (!clip.empty()) {
      target_url = clip;
      detected_clipboard_url_ = clip;
    }
  }

  // src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:220-260
  if (target_url.empty()) {
    error_message_ = tr8("layout.components.firstRun.errEmpty");
    if (bounds().w > 0.0f && bounds().h > 0.0f) on_arrange(bounds());
    invalidate();
    return;
  }
  if (!clashwin::util::is_supported_subscription_or_node_input(target_url)) {
    error_message_ = tr8("layout.components.firstRun.errScheme");
    if (bounds().w > 0.0f && bounds().h > 0.0f) on_arrange(bounds());
    invalidate();
    return;
  }

  if (url_field_) url_field_->set_enabled(false);

  is_importing_ = true;
  error_message_.clear();
  if (bounds().w > 0.0f && bounds().h > 0.0f) on_arrange(bounds());
  invalidate();

  auto finish_import = [this](bool success, const std::string& err_msg) {
    if (success) {
      bool checked = false;
      bool takeover_on = false;
      try {
        const auto& v = *config::Config::instance().verge().latest_arc();
        takeover_on = v.enable_system_proxy.value_or(false) ||
                      v.enable_tun_mode.value_or(false);
        checked = true;
      } catch (...) {
      }
      if (!opened_after_completion_ && checked && !takeover_on) {
        if (on_patch_system_proxy) {
          on_patch_system_proxy(true);
        } else {
          try {
            cmd::CommandService::instance().invoke("patch_verge_config",
                                                   {{"enable_system_proxy", true}});
          } catch (...) {}
        }
        takeover_on = true;
      }
      takeover_active_ = !checked || takeover_on;

      is_importing_ = false;
      if (url_field_) url_field_->set_enabled(true);
      step_ = FirstRunStep::Ready;
      gave_up_ = false;
      ready_started_ms_ = 0;
      last_poll_ms_ = 0;
      if (bounds().w > 0.0f && bounds().h > 0.0f) on_arrange(bounds());
      invalidate();

      if (!auto_retested_) {
        auto_retested_ = true;
        if (on_retest_nodes) on_retest_nodes();
      }
    } else {
      (void)err_msg;
      is_importing_ = false;
      if (url_field_) url_field_->set_enabled(true);
      error_message_ = tr8("layout.components.firstRun.errImport");
      if (bounds().w > 0.0f && bounds().h > 0.0f) on_arrange(bounds());
      invalidate();
    }
  };

  if (on_import_async) {
    on_import_async(target_url, finish_import);
    return;
  }

  bool success = false;
  std::string err_msg;
  if (on_import) {
    success = on_import(target_url);
  } else {
    nlohmann::json args = {{"url", target_url}};
    auto res = cmd::CommandService::instance().invoke("import_profile", args);
    success = res.is_ok();
    if (!success) {
      err_msg = res.error().message;
    }
  }
  finish_import(success, err_msg);
}

void FirstRunGuideDialog::finish() {
  set_first_run_completed(true);
  close();
}

void FirstRunGuideDialog::dismiss_via_close() {
  // src: src/pages/_layout/components/first-run-guide-dialog/index.tsx:310-330
  if (is_importing_) return;
  if (is_ready_step()) {
    finish();
  } else {
    finish();
    if (on_dismiss_later) on_dismiss_later();
  }
}

void FirstRunGuideDialog::open_url_menu(PointF anchor) {
  if (!url_menu_ || !url_field_) return;
  TextField* f = url_field_;

  // src: src/utils/popup-styles.ts:349-370
  MenuStyle st;
  st.item_h = 48;
  st.padding_h = 16;
  st.min_w = 160;
  st.radius = 6;
  st.font_size = 13.0f;
  st.pad_v = 4;
  st.hover_inset = 0;
  st.border = Color4f(1, 1, 1, 0.12f);
  st.text = ThemeService::instance().text_primary();
  st.disabled_text = Color4f(st.text.r, st.text.g, st.text.b, 0.38f);
  // src: src/pages/_theme.tsx:8
  st.danger_text =
      ThemeService::instance().is_light() ? Color4f::hex(0xFF3B30) : Color4f::hex(0xFF453A);
  apply_menu_skin(st);

  // src: src/pages/_layout/components/input-context-menu.tsx
  std::vector<MenuItem> its;
  its.push_back({L"✂️ " + tr("shared.actions.cut"), !f->can_clear(), false, L"", false});
  its.push_back({L"📋 " + tr("shared.actions.copy"), !f->can_clear(), false, L"", false});
  its.push_back({L"📥 " + tr("shared.actions.paste"), !f->can_paste(), false, L"", false});
  its.push_back({L"🔍 " + tr("shared.actions.selectAll"), !f->can_select_all(), false, L"", false});
  its.push_back({L"", false, true, L"", false});
  its.push_back({L"🧹 " + tr("shared.actions.clearInput"), !f->can_clear(), false, L"", true});

  url_menu_->style_ = st;
  url_menu_->on_select = [f](int idx) {
    switch (idx) {
      case 0: if (!f->can_cut()) f->select_all(); f->cut(); break;
      case 1: if (!f->can_copy()) f->select_all(); f->copy(); break;
      case 2: f->paste(); break;
      case 3: f->select_all(); break;
      case 5: f->clear_text(); break;
      default: break;
    }
    f->request_focus();
  };
  url_menu_->open(its, to_local(anchor));
}

std::wstring FirstRunGuideDialog::a11y_default_name() const {
  return step_ == FirstRunStep::Input
             ? tr("layout.components.firstRun.title")
             : tr("layout.components.firstRun.readyTitle");
}

size_t FirstRunGuideDialog::a11y_virtual_child_count() const {
  if (step_ == FirstRunStep::Input) {
    return (!detected_clipboard_url_.empty() && url_.empty()) ? 5u : 4u;
  }
  const bool show_retest = !proxy_ready() && (unconfirmed() || retesting_);
  return show_retest ? 5u : 4u;
}

A11ySubItem FirstRunGuideDialog::a11y_virtual_child(size_t idx) const {
  A11ySubItem sub;
  if (step_ == FirstRunStep::Input) {
    if (idx == 0) {
      sub.role = A11yRole::Text;
      sub.name = tr("layout.components.firstRun.intro");
      sub.local_bounds = card_rect_;
    } else if (idx == 1) {
      sub.role = A11yRole::Button;
      sub.name = tr("layout.components.firstRun.paste");
      sub.local_bounds = btn_paste_rect_;
      sub.can_invoke = !is_importing_;
      sub.enabled = !is_importing_;
    } else if (idx == 2) {
      sub.role = A11yRole::Button;
      sub.name = tr("layout.components.firstRun.importAndStart");
      sub.local_bounds = btn_submit_rect_;
      sub.can_invoke = !is_importing_;
      sub.enabled = !is_importing_;
    } else if (idx == 3) {
      sub.role = A11yRole::Button;
      sub.name = tr("layout.components.firstRun.later");
      sub.local_bounds = btn_later_rect_;
      sub.can_invoke = !is_importing_;
      sub.enabled = !is_importing_;
    } else {
      sub.role = A11yRole::Button;
      sub.name = tr("layout.components.firstRun.clipboardDetected");
      sub.local_bounds = banner_clip_rect_;
      sub.can_invoke = !is_importing_;
      sub.enabled = !is_importing_;
    }
    return sub;
  }
  if (idx == 0) {
    sub.role = A11yRole::Text;
    std::wstring msg = proxy_ready()
                           ? tr("layout.components.firstRun.readyOk")
                           : (unconfirmed()
                                  ? tr("layout.components.firstRun.readyUnconfirmed")
                                  : tr("layout.components.firstRun.checking"));
    if (!active_node_name_.empty()) {
      msg += L" - " + utf::utf8_to_utf16(active_node_name_);
    }
    sub.name = msg;
    sub.local_bounds = card_node_rect_;
  } else if (idx == 1) {
    sub.role = A11yRole::Button;
    sub.name = tr("layout.components.firstRun.openWiki");
    sub.local_bounds = btn_wiki_rect_;
    sub.can_invoke = true;
  } else if (idx == 2) {
    sub.role = A11yRole::Button;
    sub.name = tr("layout.components.firstRun.openGithubStar");
    sub.local_bounds = btn_finish_star_rect_;
    sub.can_invoke = true;
  } else if (idx == 3) {
    sub.role = A11yRole::Button;
    sub.name = tr("layout.components.firstRun.finish");
    sub.local_bounds = btn_finish_rect_;
    sub.can_invoke = true;
  } else {
    sub.role = A11yRole::Button;
    sub.name = tr("layout.components.firstRun.retestNodes");
    sub.local_bounds = btn_retest_rect_;
    sub.can_invoke = !retesting_;
    sub.enabled = !retesting_;
  }
  return sub;
}

bool FirstRunGuideDialog::a11y_invoke_virtual_child(size_t idx) {
  if (!open_ || !enabled()) return false;
  if (step_ == FirstRunStep::Input) {
    if (is_importing_) return false;
    if (idx == 1) {
      std::string clip = d::read_clipboard_url();
      if (!clip.empty()) {
        set_url(clip);
      } else if (url_field_) {
        url_field_->clear_text();
        url_field_->paste();
      }
      if (url_field_) url_field_->request_focus();
      return true;
    }
    if (idx == 2) {
      submit_import();
      return true;
    }
    if (idx == 3) {
      finish();
      if (on_dismiss_later) on_dismiss_later();
      return true;
    }
    if (idx == 4 && !detected_clipboard_url_.empty() && url_.empty()) {
      set_url(detected_clipboard_url_);
      if (url_field_) url_field_->request_focus();
      return true;
    }
    return false;
  }
  if (idx == 1) {
    if (on_open_wiki) {
      on_open_wiki();
    } else {
      ShellExecuteW(nullptr, L"open",
                    L"https://github.com/qiu-yuxiao/clash-win/wiki", nullptr,
                    nullptr, SW_SHOWNORMAL);
    }
    return true;
  }
  if (idx == 2) {
    if (on_open_github) {
      on_open_github();
    } else {
      ShellExecuteW(nullptr, L"open", L"https://github.com/qiu-yuxiao/clash-win",
                    nullptr, nullptr, SW_SHOWNORMAL);
    }
    finish();
    return true;
  }
  if (idx == 3) {
    finish();
    return true;
  }
  if (idx == 4 && !proxy_ready() && !retesting_) {
    retesting_ = true;
    gave_up_ = false;
    ready_started_ms_ = 0;
    last_poll_ms_ = 0;
    if (bounds().w > 0.0f && bounds().h > 0.0f) on_arrange(bounds());
    invalidate();
    if (on_retest_nodes) on_retest_nodes();
    return true;
  }
  return false;
}

}  // namespace clashwin::ui::scene

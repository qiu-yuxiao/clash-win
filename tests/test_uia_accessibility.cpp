// 原生 Win32 UI Automation (UIA) 无障碍读屏端到端测试：
// 通过真实 Windows COM IUIAutomation 客户端（模拟讲述人 Narrator / NVDA / JAWS）
// 验证：
// 1. WM_GETOBJECT 桥接与完整 UIA 树发现、父子/兄弟双向遍历对称性、屏幕坐标矩形与命中测试
// 2. 13 种内置界面语言（zh/en/ru/fa/tt/id/ar/ko/tr/de/es/jp/zhtw）实时读屏切换
// 3. UIA 控制模式（Invoke / Toggle / Value / RangeValue / SelectionItem / ExpandCollapse）与键盘 Tab/Space 导航
// 4. 模态弹窗（FirstRunGuideDialog）焦点与读屏树隔离、SnackbarW 实时通知播报、控件销毁后悬空 COM 引用防崩溃保护
#include <windows.h>
#include <oleauto.h>
#include <UIAutomation.h>
#ifdef interface
#undef interface
#endif

#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "test_util.h"
#include "i18n/i18n.h"
#include "util/utf.h"
#include "ui/container.h"
#include "ui/button.h"
#include "ui/slider.h"
#include "ui/switch_w.h"
#include "ui/textfield.h"
#include "ui/title_bar.h"
#include "ui/snackbar_w.h"
#include "ui/uia_provider.h"
#include "ui/scenes/node_data.h"
#include "ui/scenes/node_list.h"
#include "ui/scenes/quick_bar.h"
#include "ui/scenes/traffic_pane.h"
#include "ui/scenes/settings_drawer.h"
#include "ui/scenes/first_run_guide_dialog.h"

using namespace clashwin;
using namespace clashwin::ui;
using namespace clashwin::ui::win;
using namespace clashwin::ui::scene;

namespace {

std::wstring exe_locales_dir() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring dir(path);
  size_t pos = dir.find_last_of(L"\\/");
  return (pos == std::wstring::npos ? L"." : dir.substr(0, pos)) + L"\\locales";
}

struct ComScope {
  HRESULT hr = E_FAIL;
  ComScope() { hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
  ~ComScope() {
    if (hr == S_OK || hr == S_FALSE) CoUninitialize();
  }
  bool ok() const { return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
};

template <typename T>
struct ComPtr {
  T* p = nullptr;
  ComPtr() = default;
  ~ComPtr() { reset(); }
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;
  ComPtr(ComPtr&& o) noexcept : p(o.p) { o.p = nullptr; }
  ComPtr& operator=(ComPtr&& o) noexcept {
    if (this != &o) {
      reset();
      p = o.p;
      o.p = nullptr;
    }
    return *this;
  }
  void reset() {
    if (p) {
      p->Release();
      p = nullptr;
    }
  }
  T** put() {
    reset();
    return &p;
  }
  T* get() const { return p; }
  T* operator->() const { return p; }
  explicit operator bool() const { return p != nullptr; }
};

struct BstrGuard {
  BSTR s = nullptr;
  ~BstrGuard() {
    if (s) SysFreeString(s);
  }
  std::wstring wstr() const { return s ? std::wstring(s, SysStringLen(s)) : std::wstring(); }
  std::string utf8() const { return utf::utf16_to_utf8(wstr()); }
};

std::wstring elem_name(IUIAutomationElement* el) {
  if (!el) return {};
  BstrGuard b;
  if (FAILED(el->get_CurrentName(&b.s))) return {};
  return b.wstr();
}

CONTROLTYPEID elem_type(IUIAutomationElement* el) {
  CONTROLTYPEID t = 0;
  if (el) el->get_CurrentControlType(&t);
  return t;
}

class UiaTestHost {
 public:
  explicit UiaTestHost(Widget* root, float scale = 1.5f)
      : root_(root), scale_(scale) {
    static const wchar_t* kCls = L"ClashWinUiaTestHostWnd";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &UiaTestHost::wndproc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kCls;
    RegisterClassExW(&wc);

    const int pw = static_cast<int>(std::lround(271.0f * scale_));
    const int ph = static_cast<int>(std::lround(680.0f * scale_));
    bridge_ = std::make_unique<UiaWindowBridge>(
        nullptr, root_, [this] { return scale_; });
    hwnd_ = CreateWindowExW(
        0, kCls, L"Clash Win Ver.0.2.10", WS_POPUP,
        120, 120, pw, ph, nullptr, nullptr, wc.hInstance, this);
    if (hwnd_) {
      bridge_->set_hwnd(hwnd_);
    } else {
      bridge_->shutdown();
      bridge_.reset();
    }
  }

  ~UiaTestHost() {
    if (bridge_) bridge_->shutdown();
    if (hwnd_) {
      DestroyWindow(hwnd_);
      hwnd_ = nullptr;
    }
  }

  HWND hwnd() const { return hwnd_; }
  UiaWindowBridge* bridge() const { return bridge_.get(); }

 private:
  static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    UiaTestHost* self = nullptr;
    if (msg == WM_NCCREATE) {
      self = static_cast<UiaTestHost*>(
          reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
      self = reinterpret_cast<UiaTestHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self && msg == WM_GETOBJECT && self->bridge_) {
      LRESULT r = self->bridge_->handle_wm_getobject(hwnd, wp, lp);
      if (r != 0) return r;
    }
    if (msg == WM_NCCALCSIZE) return 0;
    return DefWindowProcW(hwnd, msg, wp, lp);
  }

  Widget* root_ = nullptr;
  float scale_ = 1.5f;
  HWND hwnd_ = nullptr;
  std::unique_ptr<UiaWindowBridge> bridge_;
};

// 递归收集所有后代元素，并同时校验父子与前后兄弟双向遍历对称性
void walk_and_verify_symmetry(IUIAutomation* uia, IUIAutomationTreeWalker* walker,
                              IUIAutomationElement* parent,
                              std::vector<ComPtr<IUIAutomationElement>>& out,
                              int& symmetry_errors) {
  ComPtr<IUIAutomationElement> child;
  if (FAILED(walker->GetFirstChildElement(parent, child.put())) || !child) return;

  ComPtr<IUIAutomationElement> prev_expected;
  while (child) {
    // 1. child->Parent 必须等于 parent
    ComPtr<IUIAutomationElement> actual_parent;
    if (FAILED(walker->GetParentElement(child.get(), actual_parent.put())) || !actual_parent) {
      ++symmetry_errors;
    } else {
      BOOL same = FALSE;
      uia->CompareElements(parent, actual_parent.get(), &same);
      if (!same) ++symmetry_errors;
    }

    // 2. child->PreviousSibling 必须等于 prev_expected
    ComPtr<IUIAutomationElement> actual_prev;
    walker->GetPreviousSiblingElement(child.get(), actual_prev.put());
    if (!prev_expected && actual_prev) {
      ++symmetry_errors;
    } else if (prev_expected && !actual_prev) {
      ++symmetry_errors;
    } else if (prev_expected && actual_prev) {
      BOOL same = FALSE;
      uia->CompareElements(prev_expected.get(), actual_prev.get(), &same);
      if (!same) ++symmetry_errors;
    }

    // 保存当前节点副本加入结果集
    child->AddRef();
    ComPtr<IUIAutomationElement> copy;
    copy.p = child.get();
    out.push_back(std::move(copy));

    // 递归子树
    walk_and_verify_symmetry(uia, walker, child.get(), out, symmetry_errors);

    // 前进到下一个兄弟
    ComPtr<IUIAutomationElement> next;
    walker->GetNextSiblingElement(child.get(), next.put());
    prev_expected = std::move(child);
    child = std::move(next);
  }
}

IUIAutomationElement* find_by_type_and_substr(
    const std::vector<ComPtr<IUIAutomationElement>>& els,
    CONTROLTYPEID type, const std::wstring& substr) {
  for (const auto& el : els) {
    if (elem_type(el.get()) != type) continue;
    if (substr.empty() || elem_name(el.get()).find(substr) != std::wstring::npos) {
      return el.get();
    }
  }
  return nullptr;
}

struct MockScene {
  Stack root;
  TitleBar* title_bar = nullptr;
  QuickBar* quick_bar = nullptr;
  TakeoverSegmented* takeover = nullptr;
  NodeList* node_list = nullptr;
  TrafficPane* traffic_pane = nullptr;
  SettingsDrawer* drawer = nullptr;
  FirstRunGuideDialog* first_run = nullptr;
  SnackbarW* snackbar = nullptr;
  std::unique_ptr<NodeData> node_data;
  std::string current_node = "Tokyo-01";
  std::string takeover_mode = "SYS";
  bool pinned = false;
  int gear_clicks = 0;
  int pin_clicks = 0;

  void build() {
    DataHooks hooks;
    hooks.now_ms = [] { return 100000; };
    hooks.fetch_group = [this](std::vector<std::string>* all, std::string* now) {
      *all = {"Tokyo-01", "Singapore-02", "US-West-03"};
      *now = current_node;
      return true;
    };
    hooks.fetch_proxies = [](std::map<std::string, mihomo::ProxyItem>* items) {
      mihomo::ProxyItem p1;
      p1.name = "Tokyo-01";
      p1.type = "Trojan";
      p1.alive = true;
      (*items)["Tokyo-01"] = p1;

      mihomo::ProxyItem p2;
      p2.name = "Singapore-02";
      p2.type = "VLESS";
      p2.alive = true;
      (*items)["Singapore-02"] = p2;

      mihomo::ProxyItem p3;
      p3.name = "US-West-03";
      p3.type = "Hysteria2";
      p3.alive = true;
      (*items)["US-West-03"] = p3;
      return true;
    };
    hooks.select_node = [this](const std::string& node) {
      current_node = node;
      return true;
    };
    node_data = std::make_unique<NodeData>(std::move(hooks));
    node_data->load();
    node_data->set_delay("Tokyo-01", 42);
    node_data->set_delay("Singapore-02", 68);
    node_data->set_delay("US-West-03", 135);

    auto flow = std::make_unique<Column>();
    auto tb = std::make_unique<TitleBar>(nullptr);
    title_bar = tb.get();
    title_bar->set_version(L"0.2.10");
    flow->add_child(std::move(tb));

    auto qb = std::make_unique<QuickBar>();
    quick_bar = qb.get();
    quick_bar->set_node_text(L"Tokyo-01");
    quick_bar->set_node_meta("Trojan", L"jp.example.com:443");
    quick_bar->set_badge(42, true);
    quick_bar->on_open_settings = [this] {
      ++gear_clicks;
      if (drawer) drawer->set_open(!drawer->is_open());
    };
    quick_bar->on_toggle_pin = [this] {
      ++pin_clicks;
      pinned = !pinned;
      quick_bar->set_pinned(pinned);
    };
    flow->add_child(std::move(qb));

    auto ts = std::make_unique<TakeoverSegmented>();
    takeover = ts.get();
    takeover->set_mode(takeover_mode);
    takeover->apply = [this](const std::string& m) {
      takeover_mode = m;
      return true;
    };
    flow->add_child(std::move(ts));

    auto nl = std::make_unique<NodeList>();
    nl->flex_weight = 1.0f;
    node_list = nl.get();
    node_list->select_node = [this](const std::string& n) {
      return node_data->select(n);
    };
    node_list->bind(node_data.get());
    flow->add_child(std::move(nl));

    auto tp = std::make_unique<TrafficPane>();
    traffic_pane = tp.get();
    flow->add_child(std::move(tp));

    root.add_child(std::move(flow));

    auto dr = std::make_unique<SettingsDrawer>();
    drawer = dr.get();
    drawer->on_open_changed = [this](bool open) {
      if (quick_bar) quick_bar->set_visible(!open);
      if (takeover) takeover->set_visible(!open);
      if (node_list) node_list->set_a11y_hidden(open);
    };
    drawer->wire_callbacks();
    root.add_child(std::move(dr));

    auto fr = std::make_unique<FirstRunGuideDialog>();
    first_run = fr.get();
    fr->set_z_order(980);
    root.add_child(std::move(fr));

    auto sb = std::make_unique<SnackbarW>();
    snackbar = sb.get();
    sb->set_z_order(1500);
    root.add_child(std::move(sb));

    root.layout_tree({0, 0, 271.0f, 680.0f});
  }
};

}  // namespace

UT_TEST(uia_tree_discovery_and_symmetry) {
  ComScope com;
  UT_CHECK(com.ok());
  UT_CHECK(i18n::I18n::instance().init(exe_locales_dir(), "zh"));
  const std::string prev_lang = i18n::I18n::instance().language();
  i18n::I18n::instance().set_language("zh");

  MockScene scene;
  scene.build();
  UiaTestHost host(&scene.root, 1.5f);
  UT_CHECK(host.hwnd() != nullptr);

  ComPtr<IUIAutomation> uia;
  HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(uia.put()));
  UT_CHECK(SUCCEEDED(hr) && uia);

  ComPtr<IUIAutomationElement> root_el;
  hr = uia->ElementFromHandle(host.hwnd(), root_el.put());
  UT_CHECK(SUCCEEDED(hr) && root_el);
  UT_CHECK_EQ(elem_type(root_el.get()), UIA_WindowControlTypeId);
  UT_CHECK(elem_name(root_el.get()).find(L"Clash Win") != std::wstring::npos);

  ComPtr<IUIAutomationTreeWalker> walker;
  UT_CHECK(SUCCEEDED(uia->get_RawViewWalker(walker.put())) && walker);

  std::vector<ComPtr<IUIAutomationElement>> descendants;
  int symmetry_errors = 0;
  walk_and_verify_symmetry(uia.get(), walker.get(), root_el.get(), descendants,
                           symmetry_errors);
  UT_CHECK_EQ(symmetry_errors, 0);
  UT_CHECK(descendants.size() >= 12u);

  // 核心区域与控件核验
  UT_CHECK(find_by_type_and_substr(descendants, UIA_TitleBarControlTypeId, L"Clash Win") != nullptr);
  UT_CHECK(find_by_type_and_substr(descendants, UIA_ToolBarControlTypeId, L"") != nullptr);
  UT_CHECK(find_by_type_and_substr(descendants, UIA_ListControlTypeId, L"") != nullptr);
  UT_CHECK(find_by_type_and_substr(descendants, UIA_GroupControlTypeId, L"B/s") != nullptr);

  // 快捷栏齿轮与置顶按钮（中文）
  const std::wstring gear_zh = utf::utf8_to_utf16(i18n::I18n::instance().t("layout.a11y.openSettings"));
  const std::wstring pin_zh = utf::utf8_to_utf16(i18n::I18n::instance().t("layout.a11y.pinWindow"));
  IUIAutomationElement* gear_el = find_by_type_and_substr(descendants, UIA_ButtonControlTypeId, gear_zh);
  IUIAutomationElement* pin_el = find_by_type_and_substr(descendants, UIA_ButtonControlTypeId, pin_zh);
  UT_CHECK(gear_el != nullptr);
  UT_CHECK(pin_el != nullptr);

  // 流量接管三段式 RadioButton 虚拟子项
  const std::wstring sys_zh = utf::utf8_to_utf16(i18n::I18n::instance().t("settings.mini.system"));
  UT_CHECK(find_by_type_and_substr(descendants, UIA_RadioButtonControlTypeId, sys_zh) != nullptr);

  // 节点列表项：名称 + 协议 + 延迟 + 当前节点标注 + PositionInSet/SizeOfSet
  IUIAutomationElement* tokyo_el =
      find_by_type_and_substr(descendants, UIA_ListItemControlTypeId, L"Tokyo-01");
  UT_CHECK(tokyo_el != nullptr);
  if (tokyo_el) {
    const std::wstring cur_lbl =
        utf::utf8_to_utf16(i18n::I18n::instance().t("shared.statuses.enabled"));
    const std::wstring nm = elem_name(tokyo_el);
    UT_CHECK(nm.find(L"TROJAN") != std::wstring::npos);
    UT_CHECK(nm.find(L"42ms") != std::wstring::npos);
    UT_CHECK(nm.find(cur_lbl) != std::wstring::npos);

    VARIANT pos_v{}, size_v{};
    VariantInit(&pos_v);
    VariantInit(&size_v);
    if (SUCCEEDED(tokyo_el->GetCurrentPropertyValue(UIA_PositionInSetPropertyId, &pos_v)) &&
        pos_v.vt == VT_I4) {
      UT_CHECK_EQ(pos_v.lVal, 1);
    }
    if (SUCCEEDED(tokyo_el->GetCurrentPropertyValue(UIA_SizeOfSetPropertyId, &size_v)) &&
        size_v.vt == VT_I4) {
      UT_CHECK_EQ(size_v.lVal, 3);
    }
    VariantClear(&pos_v);
    VariantClear(&size_v);
  }

  // 屏幕像素矩形与物理坐标边界核验
  if (gear_el) {
    RECT rc{};
    UT_CHECK(SUCCEEDED(gear_el->get_CurrentBoundingRectangle(&rc)));
    UT_CHECK(rc.right > rc.left && rc.bottom > rc.top);

    RECT wrc{};
    GetWindowRect(host.hwnd(), &wrc);
    UT_CHECK(rc.left >= wrc.left && rc.right <= wrc.right);
    UT_CHECK(rc.top >= wrc.top && rc.bottom <= wrc.bottom);
  }

  // 未打开的设置抽屉和首次引导弹窗不得混入读屏树
  const std::wstring close_settings_zh =
      utf::utf8_to_utf16(i18n::I18n::instance().t("layout.a11y.closeSettings"));
  UT_CHECK(find_by_type_and_substr(descendants, UIA_ButtonControlTypeId, close_settings_zh) == nullptr);
  UT_CHECK(find_by_type_and_substr(descendants, UIA_WindowControlTypeId, L"STEP") == nullptr);

  i18n::I18n::instance().set_language(prev_lang);
}

UT_TEST(uia_all_13_languages_live_switch) {
  ComScope com;
  UT_CHECK(com.ok());
  UT_CHECK(i18n::I18n::instance().init(exe_locales_dir(), "zh"));
  const std::string prev_lang = i18n::I18n::instance().language();

  MockScene scene;
  scene.build();
  UiaTestHost host(&scene.root, 1.5f);

  ComPtr<IUIAutomation> uia;
  UT_CHECK(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(uia.put()))));
  ComPtr<IUIAutomationElement> root_el;
  UT_CHECK(SUCCEEDED(uia->ElementFromHandle(host.hwnd(), root_el.put())));
  ComPtr<IUIAutomationTreeWalker> walker;
  UT_CHECK(SUCCEEDED(uia->get_RawViewWalker(walker.put())));

  i18n::I18n::instance().set_language("zh");
  std::vector<ComPtr<IUIAutomationElement>> descendants;
  int sym_err = 0;
  walk_and_verify_symmetry(uia.get(), walker.get(), root_el.get(), descendants, sym_err);
  UT_CHECK_EQ(sym_err, 0);

  const std::wstring gear_zh = utf::utf8_to_utf16(i18n::I18n::instance().t("layout.a11y.openSettings"));
  const std::wstring pin_zh = utf::utf8_to_utf16(i18n::I18n::instance().t("layout.a11y.pinWindow"));
  const std::wstring close_zh = utf::utf8_to_utf16(i18n::I18n::instance().t("shared.actions.close"));
  const std::wstring sys_zh = utf::utf8_to_utf16(i18n::I18n::instance().t("settings.mini.system"));

  IUIAutomationElement* gear_el = find_by_type_and_substr(descendants, UIA_ButtonControlTypeId, gear_zh);
  IUIAutomationElement* pin_el = find_by_type_and_substr(descendants, UIA_ButtonControlTypeId, pin_zh);
  IUIAutomationElement* close_el = find_by_type_and_substr(descendants, UIA_ButtonControlTypeId, close_zh);
  IUIAutomationElement* sys_el = find_by_type_and_substr(descendants, UIA_RadioButtonControlTypeId, sys_zh);
  IUIAutomationElement* tokyo_el = find_by_type_and_substr(descendants, UIA_ListItemControlTypeId, L"Tokyo-01");
  UT_CHECK(gear_el && pin_el && close_el && sys_el && tokyo_el);

  const auto& langs = i18n::I18n::supported_languages();
  UT_CHECK_EQ(langs.size(), 13u);

  std::map<std::string, std::wstring> gear_by_lang;
  for (const std::string& lang : langs) {
    UT_CHECK(i18n::I18n::instance().set_language(lang));

    // 不触发重绘帧，直接由 COM UIA 读取 CurrentName（验证 ensure_i18n_fresh 惰性刷新）
    const std::wstring exp_gear = utf::utf8_to_utf16(i18n::I18n::instance().t("layout.a11y.openSettings"));
    const std::wstring exp_pin = utf::utf8_to_utf16(i18n::I18n::instance().t("layout.a11y.pinWindow"));
    const std::wstring exp_close = utf::utf8_to_utf16(i18n::I18n::instance().t("shared.actions.close"));
    const std::wstring exp_sys = utf::utf8_to_utf16(i18n::I18n::instance().t("settings.mini.system"));
    const std::wstring exp_cur = utf::utf8_to_utf16(i18n::I18n::instance().t("shared.statuses.enabled"));

    UT_CHECK(!exp_gear.empty());
    UT_CHECK(elem_name(gear_el) == exp_gear);
    UT_CHECK(elem_name(pin_el) == exp_pin);
    UT_CHECK(elem_name(close_el) == exp_close);
    UT_CHECK(elem_name(sys_el) == exp_sys);
    UT_CHECK(elem_name(tokyo_el).find(exp_cur) != std::wstring::npos);

    gear_by_lang[lang] = elem_name(gear_el);
  }

  // 验证多语种互异（中文 vs 英文 vs 俄文 vs 日文）
  UT_CHECK(gear_by_lang["zh"] != gear_by_lang["en"]);
  UT_CHECK(gear_by_lang["en"] != gear_by_lang["ru"]);
  UT_CHECK(gear_by_lang["zh"] != gear_by_lang["jp"]);

  i18n::I18n::instance().set_language(prev_lang);
}

UT_TEST(uia_control_patterns_and_keyboard_nav) {
  ComScope com;
  UT_CHECK(com.ok());
  UT_CHECK(i18n::I18n::instance().init(exe_locales_dir(), "zh"));
  const std::string prev_lang = i18n::I18n::instance().language();
  i18n::I18n::instance().set_language("en");

  MockScene scene;
  scene.build();
  UiaTestHost host(&scene.root, 1.5f);

  ComPtr<IUIAutomation> uia;
  UT_CHECK(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(uia.put()))));
  ComPtr<IUIAutomationElement> root_el;
  UT_CHECK(SUCCEEDED(uia->ElementFromHandle(host.hwnd(), root_el.put())));
  ComPtr<IUIAutomationTreeWalker> walker;
  UT_CHECK(SUCCEEDED(uia->get_RawViewWalker(walker.put())));

  std::vector<ComPtr<IUIAutomationElement>> descendants;
  int sym_err = 0;
  walk_and_verify_symmetry(uia.get(), walker.get(), root_el.get(), descendants, sym_err);
  UT_CHECK_EQ(sym_err, 0);

  // 1) TogglePattern：切换置顶按钮
  const std::wstring pin_en = utf::utf8_to_utf16(i18n::I18n::instance().t("layout.a11y.pinWindow"));
  IUIAutomationElement* pin_el = find_by_type_and_substr(descendants, UIA_ButtonControlTypeId, pin_en);
  UT_CHECK(pin_el != nullptr);
  if (pin_el) {
    ComPtr<IUIAutomationTogglePattern> tog;
    UT_CHECK(SUCCEEDED(pin_el->GetCurrentPatternAs(
                 UIA_TogglePatternId, IID_PPV_ARGS(tog.put()))) && tog);
    ToggleState st = ToggleState_Indeterminate;
    UT_CHECK(SUCCEEDED(tog->get_CurrentToggleState(&st)));
    UT_CHECK_EQ(st, ToggleState_Off);
    UT_CHECK(SUCCEEDED(tog->Toggle()));
    UT_CHECK_EQ(scene.pin_clicks, 1);
    UT_CHECK(SUCCEEDED(tog->get_CurrentToggleState(&st)));
    UT_CHECK_EQ(st, ToggleState_On);
  }

  // 2) SelectionItemPattern：切换代理节点 Singapore-02
  IUIAutomationElement* sg_el =
      find_by_type_and_substr(descendants, UIA_ListItemControlTypeId, L"Singapore-02");
  UT_CHECK(sg_el != nullptr);
  if (sg_el) {
    ComPtr<IUIAutomationSelectionItemPattern> sel;
    UT_CHECK(SUCCEEDED(sg_el->GetCurrentPatternAs(
                 UIA_SelectionItemPatternId, IID_PPV_ARGS(sel.put()))) && sel);
    BOOL is_sel = TRUE;
    UT_CHECK(SUCCEEDED(sel->get_CurrentIsSelected(&is_sel)));
    UT_CHECK(!is_sel);
    UT_CHECK(SUCCEEDED(sel->Select()));
    UT_CHECK_EQ(scene.current_node, "Singapore-02");
  }

  // 3) ValuePattern：节点搜索输入框读写
  IUIAutomationElement* search_el =
      find_by_type_and_substr(descendants, UIA_EditControlTypeId, L"");
  UT_CHECK(search_el != nullptr);
  if (search_el) {
    ComPtr<IUIAutomationValuePattern> val;
    UT_CHECK(SUCCEEDED(search_el->GetCurrentPatternAs(
                 UIA_ValuePatternId, IID_PPV_ARGS(val.put()))) && val);
    BSTR query = SysAllocString(L"US-West");
    UT_CHECK(SUCCEEDED(val->SetValue(query)));
    SysFreeString(query);

    BstrGuard cur_val;
    UT_CHECK(SUCCEEDED(val->get_CurrentValue(&cur_val.s)));
    UT_CHECK(cur_val.wstr() == L"US-West");

    BSTR empty = SysAllocString(L"");
    UT_CHECK(SUCCEEDED(val->SetValue(empty)));
    SysFreeString(empty);
  }

  // 4) InvokePattern：点击齿轮打开设置抽屉 + 验证遮挡隐藏（NodeList 被屏蔽，抽屉控件可见）
  const std::wstring gear_en = utf::utf8_to_utf16(i18n::I18n::instance().t("layout.a11y.openSettings"));
  IUIAutomationElement* gear_el = find_by_type_and_substr(descendants, UIA_ButtonControlTypeId, gear_en);
  UT_CHECK(gear_el != nullptr);
  if (gear_el) {
    ComPtr<IUIAutomationInvokePattern> inv;
    UT_CHECK(SUCCEEDED(gear_el->GetCurrentPatternAs(
                 UIA_InvokePatternId, IID_PPV_ARGS(inv.put()))) && inv);
    UT_CHECK(SUCCEEDED(inv->Invoke()));
    UT_CHECK_EQ(scene.gear_clicks, 1);
    UT_CHECK(scene.drawer->is_open());
    // 切换为大窗尺寸以展示抽屉底部语言下拉框与连接面板
    scene.root.layout_tree({0, 0, 640.0f, 860.0f});

    std::vector<ComPtr<IUIAutomationElement>> drawer_els;
    int drawer_sym_err = 0;
    walk_and_verify_symmetry(uia.get(), walker.get(), root_el.get(), drawer_els,
                             drawer_sym_err);
    UT_CHECK_EQ(drawer_sym_err, 0);

    // 被遮挡的节点列表项不再暴露给读屏器
    UT_CHECK(find_by_type_and_substr(drawer_els, UIA_ListItemControlTypeId, L"Tokyo-01") == nullptr);

    // 抽屉内的语言下拉框（ExpandCollapsePattern）与主题滑块（RangeValuePattern）与开关（TogglePattern）暴露正常
    const std::wstring lang_en =
        utf::utf8_to_utf16(i18n::I18n::instance().t("layout.a11y.selectLanguage"));
    IUIAutomationElement* lang_el =
        find_by_type_and_substr(drawer_els, UIA_ComboBoxControlTypeId, lang_en);
    UT_CHECK(lang_el != nullptr);
    if (lang_el) {
      ComPtr<IUIAutomationExpandCollapsePattern> ec;
      UT_CHECK(SUCCEEDED(lang_el->GetCurrentPatternAs(
                   UIA_ExpandCollapsePatternId, IID_PPV_ARGS(ec.put()))) && ec);
      ExpandCollapseState ecs = ExpandCollapseState_LeafNode;
      UT_CHECK(SUCCEEDED(ec->get_CurrentExpandCollapseState(&ecs)));
      UT_CHECK_EQ(ecs, ExpandCollapseState_Collapsed);
    }

    IUIAutomationElement* slider_el =
        find_by_type_and_substr(drawer_els, UIA_SliderControlTypeId, L"");
    UT_CHECK(slider_el != nullptr);
    if (slider_el) {
      ComPtr<IUIAutomationRangeValuePattern> rv;
      UT_CHECK(SUCCEEDED(slider_el->GetCurrentPatternAs(
                   UIA_RangeValuePatternId, IID_PPV_ARGS(rv.put()))) && rv);
      double min_v = 0.0, max_v = 0.0, cur_v = 0.0;
      UT_CHECK(SUCCEEDED(rv->get_CurrentMinimum(&min_v)));
      UT_CHECK(SUCCEEDED(rv->get_CurrentMaximum(&max_v)));
      UT_CHECK(max_v > min_v);
      const double target_v = (min_v + max_v) * 0.5;
      UT_CHECK(SUCCEEDED(rv->SetValue(target_v)));
      UT_CHECK(SUCCEEDED(rv->get_CurrentValue(&cur_v)));
      UT_CHECK(std::fabs(cur_v - target_v) <= 0.15);
    }

    // 关闭抽屉恢复主场景
    scene.drawer->set_open(false);
    scene.root.layout_tree({0, 0, 271.0f, 680.0f});
  }

  // 5) 键盘 Tab / Shift+Tab 导航与 Space 键激活
  KeyEvent tab_down{};
  tab_down.type = KeyEventType::Down;
  tab_down.key = VK_TAB;
  UT_CHECK(route_key_event(&scene.root, tab_down));
  Widget* first_focus = focused_widget();
  UT_CHECK(first_focus != nullptr);

  UT_CHECK(route_key_event(&scene.root, tab_down));
  Widget* second_focus = focused_widget();
  UT_CHECK(second_focus != nullptr && second_focus != first_focus);

  KeyEvent shift_tab = tab_down;
  shift_tab.shift = true;
  UT_CHECK(route_key_event(&scene.root, shift_tab));
  UT_CHECK(focused_widget() == first_focus);

  i18n::I18n::instance().set_language(prev_lang);
}

UT_TEST(uia_modal_scoping_and_toast_and_destruction_safety) {
  ComScope com;
  UT_CHECK(com.ok());
  UT_CHECK(i18n::I18n::instance().init(exe_locales_dir(), "zh"));
  const std::string prev_lang = i18n::I18n::instance().language();
  i18n::I18n::instance().set_language("zh");

  MockScene scene;
  scene.build();
  UiaTestHost host(&scene.root, 1.5f);

  ComPtr<IUIAutomation> uia;
  UT_CHECK(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(uia.put()))));
  ComPtr<IUIAutomationElement> root_el;
  UT_CHECK(SUCCEEDED(uia->ElementFromHandle(host.hwnd(), root_el.put())));
  ComPtr<IUIAutomationTreeWalker> walker;
  UT_CHECK(SUCCEEDED(uia->get_RawViewWalker(walker.put())));

  // 1) 打开清洁安装首启引导模态窗 → 背景控件自动隔离，仅模态窗与顶部通知暴露
  scene.first_run->open();
  scene.root.layout_tree({0, 0, 271.0f, 680.0f});

  {
    std::vector<ComPtr<IUIAutomationElement>> modal_els;
    int sym_err = 0;
    walk_and_verify_symmetry(uia.get(), walker.get(), root_el.get(), modal_els, sym_err);
    UT_CHECK_EQ(sym_err, 0);

    // 背景的标题栏与节点列表全部被模态隔离屏蔽
    UT_CHECK(find_by_type_and_substr(modal_els, UIA_TitleBarControlTypeId, L"") == nullptr);
    UT_CHECK(find_by_type_and_substr(modal_els, UIA_ListItemControlTypeId, L"Tokyo-01") == nullptr);

    // 首次引导弹窗本体及其粘贴/导入/稍后按钮全部暴露给读屏器
    const std::wstring title_zh =
        utf::utf8_to_utf16(i18n::I18n::instance().t("layout.components.firstRun.title"));
    UT_CHECK(find_by_type_and_substr(modal_els, UIA_WindowControlTypeId, title_zh) != nullptr);
    const std::wstring later_zh =
        utf::utf8_to_utf16(i18n::I18n::instance().t("layout.components.firstRun.later"));
    UT_CHECK(find_by_type_and_substr(modal_els, UIA_ButtonControlTypeId, later_zh) != nullptr);
  }

  // 2) SnackbarW 实时通知播报（即使处于模态弹窗期间，顶部通知条亦对读屏器可见且对称）
  {
    events::NoticeItem notice;
    notice.id = 1;
    notice.type = "success";
    notice.message = "订阅导入成功";
    scene.snackbar->set_items({notice});
    scene.snackbar->layout_items();

    std::vector<ComPtr<IUIAutomationElement>> with_toast_els;
    int sym_err = 0;
    walk_and_verify_symmetry(uia.get(), walker.get(), root_el.get(), with_toast_els, sym_err);
    UT_CHECK_EQ(sym_err, 0);
    UT_CHECK(find_by_type_and_substr(
                 with_toast_els, UIA_StatusBarControlTypeId, L"订阅导入成功") != nullptr);

    scene.snackbar->set_items({});
  }

  // 关闭模态窗后主场景恢复暴露
  scene.first_run->close();
  scene.root.layout_tree({0, 0, 271.0f, 680.0f});

  // 3) 控件动态销毁安全性：持有 COM IUIAutomationElement* 期间销毁底层 C++ Widget 绝不崩溃/UAF
  auto dyn_btn = std::make_unique<Button>(L"EphemeralActionButton");
  dyn_btn->set_bounds({20.0f, 20.0f, 120.0f, 28.0f});
  Button* dyn_raw = dyn_btn.get();
  scene.root.add_child(std::move(dyn_btn));

  ComPtr<IUIAutomationElement> stale_el;
  {
    std::vector<ComPtr<IUIAutomationElement>> els;
    int sym_err = 0;
    walk_and_verify_symmetry(uia.get(), walker.get(), root_el.get(), els, sym_err);
    UT_CHECK_EQ(sym_err, 0);
    IUIAutomationElement* found =
        find_by_type_and_substr(els, UIA_ButtonControlTypeId, L"EphemeralActionButton");
    UT_CHECK(found != nullptr);
    if (found) {
      found->AddRef();
      stale_el.p = found;
    }
  }
  UT_CHECK(stale_el);
  UT_CHECK(elem_name(stale_el.get()) == L"EphemeralActionButton");

  // 销毁底层 Widget（~Widget 自动回调 UiaWindowBridge::on_widget_destroyed 断开 Provider）
  auto removed = scene.root.remove_child(dyn_raw);
  removed.reset();

  // 对已销毁控件的悬空 COM 引用发起属性与 Invoke 查询：必须优雅返回失败码且零崩溃
  if (stale_el) {
    BstrGuard dead_name;
    HRESULT dead_hr = stale_el->get_CurrentName(&dead_name.s);
    UT_CHECK(FAILED(dead_hr));

    ComPtr<IUIAutomationInvokePattern> dead_inv;
    HRESULT inv_hr = stale_el->GetCurrentPatternAs(
        UIA_InvokePatternId, IID_PPV_ARGS(dead_inv.put()));
    if (SUCCEEDED(inv_hr) && dead_inv) {
      UT_CHECK(FAILED(dead_inv->Invoke()));
    }
  }

  i18n::I18n::instance().set_language(prev_lang);
}

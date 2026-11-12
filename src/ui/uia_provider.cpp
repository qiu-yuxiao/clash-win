// Win32 UI Automation (UIA) 原生无障碍读屏引擎实现。
// 豁免: Mini 无对应物 — 零 WebView 纯 Direct2D 自绘架构下的原生 COM UIA Provider 桥接层（出处: 无）
#include "uia_provider.h"

#include <ole2.h>
#include <UIAutomation.h>
#ifdef interface
#undef interface
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "widget.h"

namespace clashwin::ui {

namespace {

// Windows 10+ UIA 扩展属性 ID（兼容旧版 SDK 头文件宏缺失）
constexpr PROPERTYID kPropPositionInSet = 30152;
constexpr PROPERTYID kPropSizeOfSet = 30153;
constexpr PROPERTYID kPropIsDialog = 30174;

// 动态解析 UiaRaiseNotificationEvent（Windows 10 RS3+ 读屏器实时通知 API）
using PfnUiaRaiseNotificationEvent = HRESULT(WINAPI*)(
    IRawElementProviderSimple* provider, int notificationKind,
    int notificationProcessing, BSTR displayString, BSTR activityId);

PfnUiaRaiseNotificationEvent resolve_notify_fn() {
  static PfnUiaRaiseNotificationEvent s_fn = []() -> PfnUiaRaiseNotificationEvent {
    HMODULE mod = GetModuleHandleW(L"UIAutomationCore.dll");
    if (!mod) mod = LoadLibraryW(L"UIAutomationCore.dll");
    if (!mod) return nullptr;
    return reinterpret_cast<PfnUiaRaiseNotificationEvent>(
        GetProcAddress(mod, "UiaRaiseNotificationEvent"));
  }();
  return s_fn;
}

CONTROLTYPEID role_to_control_type(A11yRole role) {
  switch (role) {
    case A11yRole::Window: return UIA_WindowControlTypeId;
    case A11yRole::Pane: return UIA_PaneControlTypeId;
    case A11yRole::Group: return UIA_GroupControlTypeId;
    case A11yRole::Dialog: return UIA_WindowControlTypeId;
    case A11yRole::TitleBar: return UIA_TitleBarControlTypeId;
    case A11yRole::ToolBar: return UIA_ToolBarControlTypeId;
    case A11yRole::StatusBar: return UIA_StatusBarControlTypeId;
    case A11yRole::Text: return UIA_TextControlTypeId;
    case A11yRole::Button: return UIA_ButtonControlTypeId;
    case A11yRole::CheckBox: return UIA_CheckBoxControlTypeId;
    case A11yRole::RadioButton: return UIA_RadioButtonControlTypeId;
    case A11yRole::Edit: return UIA_EditControlTypeId;
    case A11yRole::Slider: return UIA_SliderControlTypeId;
    case A11yRole::ComboBox: return UIA_ComboBoxControlTypeId;
    case A11yRole::List: return UIA_ListControlTypeId;
    case A11yRole::ListItem: return UIA_ListItemControlTypeId;
    case A11yRole::Menu: return UIA_MenuControlTypeId;
    case A11yRole::MenuItem: return UIA_MenuItemControlTypeId;
    case A11yRole::Tab: return UIA_TabControlTypeId;
    case A11yRole::TabItem: return UIA_TabItemControlTypeId;
    case A11yRole::None:
    default: return UIA_CustomControlTypeId;
  }
}

struct NodeKey {
  Widget* widget = nullptr;
  int sub_index = -1;  // -1 = 真实 Widget；>=0 = 虚拟子项
  bool is_root = false;

  bool valid() const { return is_root || widget != nullptr; }
  bool operator==(const NodeKey& o) const {
    return widget == o.widget && sub_index == o.sub_index && is_root == o.is_root;
  }
};

std::vector<Widget*> sorted_children(Widget* w) {
  std::vector<Widget*> out;
  if (!w) return out;
  out.reserve(w->child_count());
  for (size_t i = 0; i < w->child_count(); ++i) out.push_back(w->child(i));
  w->collect_transient_children(out);
  std::stable_sort(out.begin(), out.end(),
                   [](const Widget* a, const Widget* b) {
                     return a->z_order() < b->z_order();
                   });
  return out;
}

Widget* find_active_modal(Widget* w) {
  if (!w || !w->a11y_subtree_visible()) return nullptr;
  auto kids = sorted_children(w);
  for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
    if (Widget* m = find_active_modal(*it)) return m;
  }
  return w->a11y_is_modal() ? w : nullptr;
}

bool is_widget_exposed(const Widget* w, const Widget* root_w) {
  if (!w || w == root_w) return false;
  if (w->a11y_role() != A11yRole::None) return true;
  if (!w->a11y_name().empty()) return true;
  if (w->a11y_virtual_child_count() > 0) return true;
  if (w->a11y_is_focusable()) return true;
  if (w->a11y_can_invoke()) return true;
  if (w->a11y_toggle_state().has_value()) return true;
  return false;
}

bool is_descendant_of(const Widget* node, const Widget* ancestor) {
  for (const Widget* cur = node; cur; cur = cur->parent()) {
    if (cur == ancestor) return true;
  }
  return false;
}

void append_exposed_from_subtree(Widget* w, Widget* root_w,
                                 std::vector<NodeKey>& out) {
  if (!w || !w->a11y_subtree_visible()) return;
  if (is_widget_exposed(w, root_w)) {
    out.push_back(NodeKey{w, -1, false});
    return;
  }
  for (Widget* c : sorted_children(w)) {
    append_exposed_from_subtree(c, root_w, out);
  }
}

void collect_top_overlays_above_modal(Widget* cur, Widget* modal, Widget* root_w,
                                      std::vector<Widget*>& overlays) {
  if (!cur || !cur->a11y_subtree_visible() || cur == modal) return;
  const A11yRole role = cur->a11y_role();
  if ((role == A11yRole::StatusBar || role == A11yRole::Menu) &&
      cur->z_order() >= modal->z_order() && !is_descendant_of(cur, modal)) {
    overlays.push_back(cur);
    return;
  }
  for (Widget* c : sorted_children(cur)) {
    collect_top_overlays_above_modal(c, modal, root_w, overlays);
  }
}

std::vector<NodeKey> collect_children(Widget* root_w, const NodeKey& parent) {
  std::vector<NodeKey> out;
  if (!root_w || !parent.valid() || parent.sub_index >= 0) return out;

  if (parent.is_root) {
    if (Widget* modal = find_active_modal(root_w)) {
      append_exposed_from_subtree(modal, root_w, out);
      std::vector<Widget*> overlays;
      collect_top_overlays_above_modal(root_w, modal, root_w, overlays);
      for (Widget* ov : overlays) {
        append_exposed_from_subtree(ov, root_w, out);
      }
      return out;
    }
    for (Widget* c : sorted_children(root_w)) {
      append_exposed_from_subtree(c, root_w, out);
    }
    return out;
  }

  Widget* w = parent.widget;
  if (!w || !w->a11y_subtree_visible()) return out;

  const size_t vcount = w->a11y_virtual_child_count();
  for (size_t i = 0; i < vcount; ++i) {
    A11ySubItem sub = w->a11y_virtual_child(i);
    if (sub.role != A11yRole::None) {
      out.push_back(NodeKey{w, static_cast<int>(i), false});
    }
  }
  for (Widget* c : sorted_children(w)) {
    append_exposed_from_subtree(c, root_w, out);
  }
  return out;
}

bool is_widget_reachable_from_root(Widget* w, Widget* root_w) {
  if (!w || !root_w) return false;
  for (const Widget* cur = w; cur && cur != root_w; cur = cur->parent()) {
    if (!cur->a11y_subtree_visible()) return false;
    if (cur->parent() == nullptr) return false;
  }
  if (Widget* modal = find_active_modal(root_w)) {
    if (is_descendant_of(w, modal)) return true;
    std::vector<Widget*> overlays;
    collect_top_overlays_above_modal(root_w, modal, root_w, overlays);
    for (Widget* ov : overlays) {
      if (is_descendant_of(w, ov)) return true;
    }
    return false;
  }
  return is_descendant_of(w, root_w);
}

NodeKey find_parent_key(Widget* root_w, const NodeKey& child) {
  if (!root_w || !child.valid() || child.is_root || !child.widget) return {};
  if (!is_widget_reachable_from_root(child.widget, root_w)) return {};

  if (child.sub_index >= 0) {
    if (is_widget_exposed(child.widget, root_w)) {
      return NodeKey{child.widget, -1, false};
    }
  }

  if (Widget* modal = find_active_modal(root_w)) {
    if (child.sub_index < 0) {
      if (child.widget == modal) return NodeKey{nullptr, -1, true};
      std::vector<Widget*> overlays;
      collect_top_overlays_above_modal(root_w, modal, root_w, overlays);
      for (Widget* ov : overlays) {
        if (child.widget == ov) return NodeKey{nullptr, -1, true};
      }
    }
  }

  for (Widget* p = child.widget->parent(); p; p = p->parent()) {
    if (p == root_w) return NodeKey{nullptr, -1, true};
    if (Widget* modal = find_active_modal(root_w)) {
      if (p == modal && is_widget_exposed(modal, root_w)) {
        return NodeKey{modal, -1, false};
      }
    }
    if (is_widget_exposed(p, root_w)) {
      return NodeKey{p, -1, false};
    }
  }
  return {};
}

class UiaRootProvider;
class UiaElementProvider;

struct UiaBridgeState {
  HWND hwnd = nullptr;
  Widget* root_widget = nullptr;
  std::function<float()> dpi_scale_fn;
  UiaRootProvider* root_provider = nullptr;  // COM 引用由 UiaWindowBridge 持有
  bool alive = true;
  int next_runtime_id = 1;
  std::unordered_map<Widget*, std::vector<UiaElementProvider*>> live_providers;

  float scale() const {
    float s = dpi_scale_fn ? dpi_scale_fn() : 1.0f;
    return s > 0.01f ? s : 1.0f;
  }

  UiaRect dip_to_screen(const RectF& dip) const {
    UiaRect out{};
    if (!hwnd) return out;
    POINT pt{0, 0};
    ClientToScreen(hwnd, &pt);
    const double s = static_cast<double>(scale());
    out.left = static_cast<double>(pt.x) + static_cast<double>(dip.x) * s;
    out.top = static_cast<double>(pt.y) + static_cast<double>(dip.y) * s;
    out.width = std::max(0.0, static_cast<double>(dip.w) * s);
    out.height = std::max(0.0, static_cast<double>(dip.h) * s);
    return out;
  }

  PointF screen_to_dip(double sx, double sy) const {
    POINT pt{0, 0};
    if (hwnd) ClientToScreen(hwnd, &pt);
    const float s = scale();
    return PointF{static_cast<float>(sx - static_cast<double>(pt.x)) / s,
                  static_cast<float>(sy - static_cast<double>(pt.y)) / s};
  }
};

std::vector<std::shared_ptr<UiaBridgeState>>& active_bridges() {
  static std::vector<std::shared_ptr<UiaBridgeState>> s_bridges;
  return s_bridges;
}

// ============================================================================
// UiaElementProvider：单个 Widget 或虚拟子项的 COM Provider
// ============================================================================
class UiaElementProvider final : public IRawElementProviderSimple,
                                 public IRawElementProviderFragment,
                                 public IInvokeProvider,
                                 public IToggleProvider,
                                 public IValueProvider,
                                 public IRangeValueProvider,
                                 public ISelectionItemProvider,
                                 public IExpandCollapseProvider {
 public:
  UiaElementProvider(std::shared_ptr<UiaBridgeState> state, Widget* w,
                     int sub_index, int runtime_id)
      : state_(std::move(state)),
        widget_(w),
        sub_index_(sub_index),
        runtime_id_(runtime_id) {}

  ~UiaElementProvider() {
    if (widget_ && state_) {
      auto it = state_->live_providers.find(widget_);
      if (it != state_->live_providers.end()) {
        auto& vec = it->second;
        vec.erase(std::remove(vec.begin(), vec.end(), this), vec.end());
        if (vec.empty()) state_->live_providers.erase(it);
      }
    }
  }

  Widget* widget() const { return widget_; }
  int sub_index() const { return sub_index_; }
  void detach_widget() { widget_ = nullptr; }

  bool is_available() const {
    if (!state_ || !state_->alive || !state_->root_widget || !widget_) return false;
    if (sub_index_ >= 0) {
      if (static_cast<size_t>(sub_index_) >= widget_->a11y_virtual_child_count())
        return false;
    }
    return true;
  }

  // ---- IUnknown ----
  IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == __uuidof(IUnknown) ||
        riid == __uuidof(IRawElementProviderSimple)) {
      *ppv = static_cast<IRawElementProviderSimple*>(this);
    } else if (riid == __uuidof(IRawElementProviderFragment)) {
      *ppv = static_cast<IRawElementProviderFragment*>(this);
    } else if (riid == __uuidof(IInvokeProvider)) {
      *ppv = static_cast<IInvokeProvider*>(this);
    } else if (riid == __uuidof(IToggleProvider)) {
      *ppv = static_cast<IToggleProvider*>(this);
    } else if (riid == __uuidof(IValueProvider)) {
      *ppv = static_cast<IValueProvider*>(this);
    } else if (riid == __uuidof(IRangeValueProvider)) {
      *ppv = static_cast<IRangeValueProvider*>(this);
    } else if (riid == __uuidof(ISelectionItemProvider)) {
      *ppv = static_cast<ISelectionItemProvider*>(this);
    } else if (riid == __uuidof(IExpandCollapseProvider)) {
      *ppv = static_cast<IExpandCollapseProvider*>(this);
    } else {
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }

  IFACEMETHODIMP_(ULONG) AddRef() override {
    return static_cast<ULONG>(++ref_count_);
  }

  IFACEMETHODIMP_(ULONG) Release() override {
    const ULONG c = static_cast<ULONG>(--ref_count_);
    if (c == 0) delete this;
    return c;
  }

  // ---- IRawElementProviderSimple ----
  IFACEMETHODIMP get_ProviderOptions(ProviderOptions* pRetVal) override {
    if (!pRetVal) return E_POINTER;
    *pRetVal = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider |
                                            ProviderOptions_UseComThreading);
    return S_OK;
  }

  IFACEMETHODIMP GetPatternProvider(PATTERNID patternId,
                                    IUnknown** pRetVal) override {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;

    if (patternId == UIA_InvokePatternId && can_invoke()) {
      *pRetVal = static_cast<IInvokeProvider*>(this);
      AddRef();
      return S_OK;
    }
    if (patternId == UIA_TogglePatternId && toggle_state().has_value()) {
      *pRetVal = static_cast<IToggleProvider*>(this);
      AddRef();
      return S_OK;
    }
    if (patternId == UIA_ValuePatternId && supports_value_pattern()) {
      *pRetVal = static_cast<IValueProvider*>(this);
      AddRef();
      return S_OK;
    }
    if (patternId == UIA_RangeValuePatternId && sub_index_ < 0 &&
        widget_->a11y_range().has_value()) {
      *pRetVal = static_cast<IRangeValueProvider*>(this);
      AddRef();
      return S_OK;
    }
    if (patternId == UIA_SelectionItemPatternId && selected_state().has_value()) {
      *pRetVal = static_cast<ISelectionItemProvider*>(this);
      AddRef();
      return S_OK;
    }
    if (patternId == UIA_ExpandCollapsePatternId && expanded_state().has_value()) {
      *pRetVal = static_cast<IExpandCollapseProvider*>(this);
      AddRef();
      return S_OK;
    }
    return S_OK;
  }

  IFACEMETHODIMP GetPropertyValue(PROPERTYID propertyId,
                                  VARIANT* pRetVal) override {
    if (!pRetVal) return E_POINTER;
    VariantInit(pRetVal);
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;

    const A11yRole role = resolved_role();
    switch (propertyId) {
      case UIA_ControlTypePropertyId:
        pRetVal->vt = VT_I4;
        pRetVal->lVal = role_to_control_type(role);
        return S_OK;

      case UIA_NamePropertyId: {
        const std::wstring n = resolved_name();
        pRetVal->vt = VT_BSTR;
        pRetVal->bstrVal = SysAllocString(n.c_str());
        return S_OK;
      }

      case UIA_AutomationIdPropertyId: {
        const std::wstring id = resolved_automation_id();
        if (!id.empty()) {
          pRetVal->vt = VT_BSTR;
          pRetVal->bstrVal = SysAllocString(id.c_str());
        }
        return S_OK;
      }

      case UIA_HelpTextPropertyId: {
        const std::wstring h = resolved_help_text();
        if (!h.empty()) {
          pRetVal->vt = VT_BSTR;
          pRetVal->bstrVal = SysAllocString(h.c_str());
        }
        return S_OK;
      }

      case UIA_IsEnabledPropertyId:
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = resolved_enabled() ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;

      case UIA_IsKeyboardFocusablePropertyId:
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = resolved_focusable() ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;

      case UIA_HasKeyboardFocusPropertyId:
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = resolved_has_focus() ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;

      case UIA_IsControlElementPropertyId:
      case UIA_IsContentElementPropertyId:
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = VARIANT_TRUE;
        return S_OK;

      case kPropIsDialog:
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = (role == A11yRole::Dialog) ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;

      case kPropPositionInSet: {
        auto ps = resolved_pos_in_set();
        if (ps && ps->pos > 0) {
          pRetVal->vt = VT_I4;
          pRetVal->lVal = ps->pos;
        }
        return S_OK;
      }

      case kPropSizeOfSet: {
        auto ps = resolved_pos_in_set();
        if (ps && ps->size > 0) {
          pRetVal->vt = VT_I4;
          pRetVal->lVal = ps->size;
        }
        return S_OK;
      }

      default:
        return S_OK;
    }
  }

  IFACEMETHODIMP get_HostRawElementProvider(
      IRawElementProviderSimple** pRetVal) override {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    return S_OK;
  }

  // ---- IRawElementProviderFragment ----
  IFACEMETHODIMP Navigate(NavigateDirection direction,
                          IRawElementProviderFragment** pRetVal) override;

  IFACEMETHODIMP GetRuntimeId(SAFEARRAY** pRetVal) override {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    int rId[2] = {UiaAppendRuntimeId, runtime_id_};
    SAFEARRAY* psa = SafeArrayCreateVector(VT_I4, 0, 2);
    if (!psa) return E_OUTOFMEMORY;
    for (LONG i = 0; i < 2; ++i) {
      SafeArrayPutElement(psa, &i, &rId[i]);
    }
    *pRetVal = psa;
    return S_OK;
  }

  IFACEMETHODIMP get_BoundingRectangle(UiaRect* pRetVal) override {
    if (!pRetVal) return E_POINTER;
    *pRetVal = {};
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    *pRetVal = state_->dip_to_screen(resolved_global_dip_bounds());
    return S_OK;
  }

  IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** pRetVal) override {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    return S_OK;
  }

  IFACEMETHODIMP SetFocus() override {
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    if (sub_index_ < 0 && widget_->a11y_is_focusable()) {
      widget_->request_focus();
    }
    return S_OK;
  }

  IFACEMETHODIMP get_FragmentRoot(
      IRawElementProviderFragmentRoot** pRetVal) override;

  // ---- IInvokeProvider ----
  IFACEMETHODIMP Invoke() override {
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    bool ok = false;
    if (sub_index_ >= 0) {
      ok = widget_->a11y_invoke_virtual_child(static_cast<size_t>(sub_index_));
    } else {
      ok = widget_->a11y_invoke();
    }
    if (!ok) return UIA_E_NOTSUPPORTED;
    if (UiaClientsAreListening()) {
      UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(this),
                              UIA_Invoke_InvokedEventId);
    }
    return S_OK;
  }

  // ---- IToggleProvider ----
  IFACEMETHODIMP Toggle() override {
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    bool ok = false;
    if (sub_index_ >= 0) {
      ok = widget_->a11y_invoke_virtual_child(static_cast<size_t>(sub_index_));
    } else {
      ok = widget_->a11y_toggle();
    }
    return ok ? S_OK : UIA_E_NOTSUPPORTED;
  }

  IFACEMETHODIMP get_ToggleState(ToggleState* pRetVal) override {
    if (!pRetVal) return E_POINTER;
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    auto st = toggle_state();
    if (!st.has_value()) return UIA_E_NOTSUPPORTED;
    *pRetVal = *st ? ToggleState_On : ToggleState_Off;
    return S_OK;
  }

  // ---- IValueProvider ----
  IFACEMETHODIMP SetValue(LPCWSTR val) override {
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    const std::wstring s = val ? val : L"";
    bool ok = false;
    if (sub_index_ >= 0) {
      ok = widget_->a11y_set_virtual_child_value(static_cast<size_t>(sub_index_), s);
    } else {
      ok = widget_->a11y_set_value(s);
    }
    return ok ? S_OK : UIA_E_NOTSUPPORTED;
  }

  IFACEMETHODIMP get_Value(BSTR* pRetVal) override {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    const std::wstring v = resolved_value();
    *pRetVal = SysAllocString(v.c_str());
    return S_OK;
  }

  IFACEMETHODIMP get_IsReadOnly(BOOL* pRetVal) override {
    if (!pRetVal) return E_POINTER;
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    const bool writable = resolved_can_set_value() ||
                          (sub_index_ < 0 && widget_->enabled() &&
                           widget_->a11y_range().has_value());
    *pRetVal = writable ? FALSE : TRUE;
    return S_OK;
  }

  // ---- IRangeValueProvider ----
  IFACEMETHODIMP SetValue(double val) override {
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    if (sub_index_ >= 0) return UIA_E_NOTSUPPORTED;
    return widget_->a11y_set_range(val) ? S_OK : UIA_E_NOTSUPPORTED;
  }

  IFACEMETHODIMP get_Value(double* pRetVal) override {
    if (!pRetVal) return E_POINTER;
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    auto r = (sub_index_ < 0) ? widget_->a11y_range() : std::nullopt;
    if (!r) return UIA_E_NOTSUPPORTED;
    *pRetVal = r->val;
    return S_OK;
  }

  IFACEMETHODIMP get_Minimum(double* pRetVal) override {
    if (!pRetVal) return E_POINTER;
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    auto r = (sub_index_ < 0) ? widget_->a11y_range() : std::nullopt;
    if (!r) return UIA_E_NOTSUPPORTED;
    *pRetVal = r->min_val;
    return S_OK;
  }

  IFACEMETHODIMP get_Maximum(double* pRetVal) override {
    if (!pRetVal) return E_POINTER;
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    auto r = (sub_index_ < 0) ? widget_->a11y_range() : std::nullopt;
    if (!r) return UIA_E_NOTSUPPORTED;
    *pRetVal = r->max_val;
    return S_OK;
  }

  IFACEMETHODIMP get_LargeChange(double* pRetVal) override {
    if (!pRetVal) return E_POINTER;
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    auto r = (sub_index_ < 0) ? widget_->a11y_range() : std::nullopt;
    if (!r) return UIA_E_NOTSUPPORTED;
    *pRetVal = r->large_step;
    return S_OK;
  }

  IFACEMETHODIMP get_SmallChange(double* pRetVal) override {
    if (!pRetVal) return E_POINTER;
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    auto r = (sub_index_ < 0) ? widget_->a11y_range() : std::nullopt;
    if (!r) return UIA_E_NOTSUPPORTED;
    *pRetVal = r->small_step;
    return S_OK;
  }

  // ---- ISelectionItemProvider ----
  IFACEMETHODIMP Select() override {
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    bool ok = false;
    if (sub_index_ >= 0) {
      ok = widget_->a11y_invoke_virtual_child(static_cast<size_t>(sub_index_));
    } else {
      ok = widget_->a11y_select();
      if (!ok) ok = widget_->a11y_invoke();
    }
    if (!ok) return UIA_E_NOTSUPPORTED;
    if (UiaClientsAreListening()) {
      UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(this),
                              UIA_SelectionItem_ElementSelectedEventId);
    }
    return S_OK;
  }

  IFACEMETHODIMP AddToSelection() override { return Select(); }
  IFACEMETHODIMP RemoveFromSelection() override { return UIA_E_NOTSUPPORTED; }

  IFACEMETHODIMP get_IsSelected(BOOL* pRetVal) override {
    if (!pRetVal) return E_POINTER;
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    auto sel = selected_state();
    if (!sel.has_value()) return UIA_E_NOTSUPPORTED;
    *pRetVal = *sel ? TRUE : FALSE;
    return S_OK;
  }

  IFACEMETHODIMP get_SelectionContainer(
      IRawElementProviderSimple** pRetVal) override;

  // ---- IExpandCollapseProvider ----
  IFACEMETHODIMP Expand() override {
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    if (sub_index_ >= 0) {
      return widget_->a11y_invoke_virtual_child(static_cast<size_t>(sub_index_))
                 ? S_OK
                 : UIA_E_NOTSUPPORTED;
    }
    return widget_->a11y_expand_collapse(true) ? S_OK : UIA_E_NOTSUPPORTED;
  }

  IFACEMETHODIMP Collapse() override {
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    if (sub_index_ >= 0) {
      return widget_->a11y_invoke_virtual_child(static_cast<size_t>(sub_index_))
                 ? S_OK
                 : UIA_E_NOTSUPPORTED;
    }
    return widget_->a11y_expand_collapse(false) ? S_OK : UIA_E_NOTSUPPORTED;
  }

  IFACEMETHODIMP get_ExpandCollapseState(
      ExpandCollapseState* pRetVal) override {
    if (!pRetVal) return E_POINTER;
    if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
    auto ex = expanded_state();
    if (!ex.has_value()) return UIA_E_NOTSUPPORTED;
    *pRetVal = *ex ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
    return S_OK;
  }

  RectF resolved_global_dip_bounds() const {
    if (!widget_) return {};
    if (sub_index_ >= 0) {
      A11ySubItem sub = widget_->a11y_virtual_child(static_cast<size_t>(sub_index_));
      RectF g = widget_->global_bounds();
      return RectF{g.x + sub.local_bounds.x, g.y + sub.local_bounds.y,
                   sub.local_bounds.w, sub.local_bounds.h};
    }
    return widget_->global_bounds();
  }

 private:
  A11ySubItem cur_sub() const {
    widget_->ensure_i18n_fresh();
    return widget_->a11y_virtual_child(static_cast<size_t>(sub_index_));
  }

  A11yRole resolved_role() const {
    return sub_index_ >= 0 ? cur_sub().role : widget_->a11y_role();
  }

  std::wstring resolved_name() const {
    return sub_index_ >= 0 ? cur_sub().name : widget_->a11y_name();
  }

  std::wstring resolved_automation_id() const {
    return sub_index_ >= 0 ? cur_sub().automation_id : widget_->a11y_automation_id();
  }

  std::wstring resolved_help_text() const {
    return sub_index_ >= 0 ? cur_sub().help_text : widget_->a11y_help_text();
  }

  std::wstring resolved_value() const {
    return sub_index_ >= 0 ? cur_sub().value : widget_->a11y_value();
  }

  bool resolved_enabled() const {
    return sub_index_ >= 0 ? (widget_->enabled() && cur_sub().enabled)
                           : widget_->enabled();
  }

  bool resolved_focusable() const {
    return sub_index_ >= 0 ? (widget_->enabled() && cur_sub().focusable)
                           : widget_->a11y_is_focusable();
  }

  bool resolved_has_focus() const {
    return sub_index_ >= 0 ? cur_sub().has_focus : widget_->focused();
  }

  bool can_invoke() const {
    return sub_index_ >= 0 ? cur_sub().can_invoke : widget_->a11y_can_invoke();
  }

  std::optional<bool> toggle_state() const {
    return sub_index_ >= 0 ? cur_sub().toggle_state : widget_->a11y_toggle_state();
  }

  std::optional<bool> selected_state() const {
    return sub_index_ >= 0 ? cur_sub().selected : widget_->a11y_is_selected();
  }

  std::optional<bool> expanded_state() const {
    return sub_index_ >= 0 ? cur_sub().expanded : widget_->a11y_expanded();
  }

  bool resolved_can_set_value() const {
    return sub_index_ >= 0 ? cur_sub().can_set_value : widget_->a11y_can_set_value();
  }

  bool supports_value_pattern() const {
    const A11yRole role = resolved_role();
    if (role == A11yRole::Edit || role == A11yRole::ComboBox) return true;
    if (resolved_can_set_value()) return true;
    return !resolved_value().empty() && sub_index_ >= 0;
  }

  std::optional<A11yPosInSet> resolved_pos_in_set() const {
    if (sub_index_ >= 0) {
      auto sub = cur_sub();
      if (sub.pos_in_set > 0 && sub.size_of_set > 0) {
        return A11yPosInSet{sub.pos_in_set, sub.size_of_set};
      }
      return std::nullopt;
    }
    return widget_->a11y_pos_in_set();
  }

  std::atomic<ULONG> ref_count_{1};
  std::shared_ptr<UiaBridgeState> state_;
  Widget* widget_ = nullptr;
  int sub_index_ = -1;
  int runtime_id_ = 0;
};

UiaElementProvider* get_or_create_element(
    const std::shared_ptr<UiaBridgeState>& state, Widget* w, int sub_index) {
  if (!state || !state->alive || !w) return nullptr;
  auto& vec = state->live_providers[w];
  for (UiaElementProvider* p : vec) {
    if (p->widget() == w && p->sub_index() == sub_index) {
      p->AddRef();
      return p;
    }
  }
  auto* created =
      new UiaElementProvider(state, w, sub_index, state->next_runtime_id++);
  vec.push_back(created);
  return created;  // 初始引用计数为 1
}

// ============================================================================
// UiaRootProvider：HWND 根 FragmentRoot
// ============================================================================
class UiaRootProvider final : public IRawElementProviderSimple,
                              public IRawElementProviderFragment,
                              public IRawElementProviderFragmentRoot {
 public:
  explicit UiaRootProvider(std::shared_ptr<UiaBridgeState> state)
      : state_(std::move(state)) {}

  // ---- IUnknown ----
  IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == __uuidof(IUnknown) ||
        riid == __uuidof(IRawElementProviderSimple)) {
      *ppv = static_cast<IRawElementProviderSimple*>(this);
    } else if (riid == __uuidof(IRawElementProviderFragment)) {
      *ppv = static_cast<IRawElementProviderFragment*>(this);
    } else if (riid == __uuidof(IRawElementProviderFragmentRoot)) {
      *ppv = static_cast<IRawElementProviderFragmentRoot*>(this);
    } else {
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }

  IFACEMETHODIMP_(ULONG) AddRef() override {
    return static_cast<ULONG>(++ref_count_);
  }

  IFACEMETHODIMP_(ULONG) Release() override {
    const ULONG c = static_cast<ULONG>(--ref_count_);
    if (c == 0) delete this;
    return c;
  }

  // ---- IRawElementProviderSimple ----
  IFACEMETHODIMP get_ProviderOptions(ProviderOptions* pRetVal) override {
    if (!pRetVal) return E_POINTER;
    *pRetVal = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider |
                                            ProviderOptions_UseComThreading);
    return S_OK;
  }

  IFACEMETHODIMP GetPatternProvider(PATTERNID, IUnknown** pRetVal) override {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    return S_OK;
  }

  IFACEMETHODIMP GetPropertyValue(PROPERTYID propertyId,
                                  VARIANT* pRetVal) override {
    if (!pRetVal) return E_POINTER;
    VariantInit(pRetVal);
    if (!state_ || !state_->alive) return UIA_E_ELEMENTNOTAVAILABLE;

    switch (propertyId) {
      case UIA_ControlTypePropertyId:
        pRetVal->vt = VT_I4;
        pRetVal->lVal = UIA_WindowControlTypeId;
        return S_OK;
      case UIA_NamePropertyId: {
        wchar_t title[256]{};
        if (state_->hwnd) GetWindowTextW(state_->hwnd, title, 255);
        std::wstring name = title[0] ? title : L"Clash Win";
        if (state_->root_widget && !state_->root_widget->a11y_name().empty()) {
          name = state_->root_widget->a11y_name();
        }
        pRetVal->vt = VT_BSTR;
        pRetVal->bstrVal = SysAllocString(name.c_str());
        return S_OK;
      }
      case UIA_AutomationIdPropertyId:
        pRetVal->vt = VT_BSTR;
        pRetVal->bstrVal = SysAllocString(L"ClashWinMainWindow");
        return S_OK;
      case UIA_IsControlElementPropertyId:
      case UIA_IsContentElementPropertyId:
      case UIA_IsEnabledPropertyId:
        pRetVal->vt = VT_BOOL;
        pRetVal->boolVal = VARIANT_TRUE;
        return S_OK;
      default:
        return S_OK;
    }
  }

  IFACEMETHODIMP get_HostRawElementProvider(
      IRawElementProviderSimple** pRetVal) override {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    if (!state_ || !state_->alive || !state_->hwnd) return UIA_E_ELEMENTNOTAVAILABLE;
    return UiaHostProviderFromHwnd(state_->hwnd, pRetVal);
  }

  // ---- IRawElementProviderFragment ----
  IFACEMETHODIMP Navigate(NavigateDirection direction,
                          IRawElementProviderFragment** pRetVal) override {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    if (!state_ || !state_->alive || !state_->root_widget)
      return UIA_E_ELEMENTNOTAVAILABLE;

    if (direction == NavigateDirection_FirstChild ||
        direction == NavigateDirection_LastChild) {
      auto kids = collect_children(state_->root_widget, NodeKey{nullptr, -1, true});
      if (kids.empty()) return S_OK;
      const NodeKey& k =
          (direction == NavigateDirection_FirstChild) ? kids.front() : kids.back();
      if (auto* elem = get_or_create_element(state_, k.widget, k.sub_index)) {
        *pRetVal = static_cast<IRawElementProviderFragment*>(elem);
      }
    }
    return S_OK;
  }

  IFACEMETHODIMP GetRuntimeId(SAFEARRAY** pRetVal) override {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;  // HWND 宿主元素按规范返回 nullptr，由系统接管窗口 RuntimeId
    return S_OK;
  }

  IFACEMETHODIMP get_BoundingRectangle(UiaRect* pRetVal) override {
    if (!pRetVal) return E_POINTER;
    *pRetVal = {};
    if (!state_ || !state_->alive || !state_->hwnd) return UIA_E_ELEMENTNOTAVAILABLE;
    RECT rc{};
    GetClientRect(state_->hwnd, &rc);
    POINT pt{0, 0};
    ClientToScreen(state_->hwnd, &pt);
    pRetVal->left = static_cast<double>(pt.x);
    pRetVal->top = static_cast<double>(pt.y);
    pRetVal->width = static_cast<double>(std::max<LONG>(0, rc.right - rc.left));
    pRetVal->height = static_cast<double>(std::max<LONG>(0, rc.bottom - rc.top));
    return S_OK;
  }

  IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** pRetVal) override {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    return S_OK;
  }

  IFACEMETHODIMP SetFocus() override {
    if (state_ && state_->alive && state_->hwnd) ::SetFocus(state_->hwnd);
    return S_OK;
  }

  IFACEMETHODIMP get_FragmentRoot(
      IRawElementProviderFragmentRoot** pRetVal) override {
    if (!pRetVal) return E_POINTER;
    *pRetVal = static_cast<IRawElementProviderFragmentRoot*>(this);
    AddRef();
    return S_OK;
  }

  // ---- IRawElementProviderFragmentRoot ----
  IFACEMETHODIMP ElementProviderFromPoint(
      double x, double y, IRawElementProviderFragment** pRetVal) override {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    if (!state_ || !state_->alive || !state_->root_widget)
      return UIA_E_ELEMENTNOTAVAILABLE;

    const PointF dip = state_->screen_to_dip(x, y);
    NodeKey hit = hit_test_node(NodeKey{nullptr, -1, true}, dip);
    if (hit.valid() && !hit.is_root) {
      if (auto* elem = get_or_create_element(state_, hit.widget, hit.sub_index)) {
        *pRetVal = static_cast<IRawElementProviderFragment*>(elem);
      }
    }
    return S_OK;
  }

  IFACEMETHODIMP GetFocus(IRawElementProviderFragment** pRetVal) override {
    if (!pRetVal) return E_POINTER;
    *pRetVal = nullptr;
    if (!state_ || !state_->alive || !state_->root_widget)
      return UIA_E_ELEMENTNOTAVAILABLE;

    Widget* f = focused_widget();
    if (f && is_widget_reachable_from_root(f, state_->root_widget) &&
        is_widget_exposed(f, state_->root_widget)) {
      if (auto* elem = get_or_create_element(state_, f, -1)) {
        *pRetVal = static_cast<IRawElementProviderFragment*>(elem);
      }
    }
    return S_OK;
  }

 private:
  NodeKey hit_test_node(const NodeKey& cur, PointF dip) const {
    auto kids = collect_children(state_->root_widget, cur);
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
      const NodeKey& k = *it;
      RectF gb{};
      if (k.sub_index >= 0) {
        A11ySubItem sub =
            k.widget->a11y_virtual_child(static_cast<size_t>(k.sub_index));
        RectF wg = k.widget->global_bounds();
        gb = {wg.x + sub.local_bounds.x, wg.y + sub.local_bounds.y,
              sub.local_bounds.w, sub.local_bounds.h};
      } else {
        gb = k.widget->global_bounds();
      }
      if (gb.contains(dip.x, dip.y)) {
        if (k.sub_index < 0) {
          NodeKey deeper = hit_test_node(k, dip);
          if (deeper.valid()) return deeper;
        }
        return k;
      }
    }
    return {};
  }

  std::atomic<ULONG> ref_count_{1};
  std::shared_ptr<UiaBridgeState> state_;
};

IFACEMETHODIMP UiaElementProvider::get_FragmentRoot(
    IRawElementProviderFragmentRoot** pRetVal) {
  if (!pRetVal) return E_POINTER;
  *pRetVal = nullptr;
  if (!is_available() || !state_->root_provider) return UIA_E_ELEMENTNOTAVAILABLE;
  *pRetVal = static_cast<IRawElementProviderFragmentRoot*>(state_->root_provider);
  (*pRetVal)->AddRef();
  return S_OK;
}

IFACEMETHODIMP UiaElementProvider::get_SelectionContainer(
    IRawElementProviderSimple** pRetVal) {
  if (!pRetVal) return E_POINTER;
  *pRetVal = nullptr;
  if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;
  NodeKey pk =
      find_parent_key(state_->root_widget, NodeKey{widget_, sub_index_, false});
  if (pk.valid() && !pk.is_root && pk.widget) {
    if (auto* p = get_or_create_element(state_, pk.widget, pk.sub_index)) {
      *pRetVal = static_cast<IRawElementProviderSimple*>(p);
    }
  }
  return S_OK;
}

IFACEMETHODIMP UiaElementProvider::Navigate(
    NavigateDirection direction, IRawElementProviderFragment** pRetVal) {
  if (!pRetVal) return E_POINTER;
  *pRetVal = nullptr;
  if (!is_available()) return UIA_E_ELEMENTNOTAVAILABLE;

  const NodeKey self_key{widget_, sub_index_, false};
  Widget* root_w = state_->root_widget;

  switch (direction) {
    case NavigateDirection_Parent: {
      NodeKey pk = find_parent_key(root_w, self_key);
      if (!pk.valid()) return S_OK;
      if (pk.is_root) {
        if (state_->root_provider) {
          *pRetVal =
              static_cast<IRawElementProviderFragment*>(state_->root_provider);
          (*pRetVal)->AddRef();
        }
        return S_OK;
      }
      if (auto* elem = get_or_create_element(state_, pk.widget, pk.sub_index)) {
        *pRetVal = static_cast<IRawElementProviderFragment*>(elem);
      }
      return S_OK;
    }

    case NavigateDirection_FirstChild:
    case NavigateDirection_LastChild: {
      auto kids = collect_children(root_w, self_key);
      if (kids.empty()) return S_OK;
      const NodeKey& k =
          (direction == NavigateDirection_FirstChild) ? kids.front() : kids.back();
      if (auto* elem = get_or_create_element(state_, k.widget, k.sub_index)) {
        *pRetVal = static_cast<IRawElementProviderFragment*>(elem);
      }
      return S_OK;
    }

    case NavigateDirection_NextSibling:
    case NavigateDirection_PreviousSibling: {
      NodeKey pk = find_parent_key(root_w, self_key);
      if (!pk.valid()) return S_OK;
      auto siblings = collect_children(root_w, pk);
      for (size_t i = 0; i < siblings.size(); ++i) {
        if (siblings[i] == self_key) {
          if (direction == NavigateDirection_NextSibling &&
              i + 1 < siblings.size()) {
            const NodeKey& nk = siblings[i + 1];
            if (auto* elem =
                    get_or_create_element(state_, nk.widget, nk.sub_index)) {
              *pRetVal = static_cast<IRawElementProviderFragment*>(elem);
            }
          } else if (direction == NavigateDirection_PreviousSibling && i > 0) {
            const NodeKey& nk = siblings[i - 1];
            if (auto* elem =
                    get_or_create_element(state_, nk.widget, nk.sub_index)) {
              *pRetVal = static_cast<IRawElementProviderFragment*>(elem);
            }
          }
          break;
        }
      }
      return S_OK;
    }

    default:
      return S_OK;
  }
}

}  // namespace

// ============================================================================
// 全局无障碍生命周期与事件钩子
// ============================================================================
namespace uia {

void on_widget_destroyed(Widget* w) {
  if (!w) return;
  for (auto& state : active_bridges()) {
    if (!state) continue;
    if (state->root_widget == w) {
      state->alive = false;
      state->root_widget = nullptr;
    }
    auto it = state->live_providers.find(w);
    if (it != state->live_providers.end()) {
      for (UiaElementProvider* p : it->second) {
        if (p) p->detach_widget();
      }
      state->live_providers.erase(it);
    }
  }
}

void on_widget_focused(Widget* w) {
  if (!w || !UiaClientsAreListening()) return;
  for (auto& state : active_bridges()) {
    if (!state || !state->alive || !state->root_widget) continue;
    if (is_widget_reachable_from_root(w, state->root_widget) &&
        is_widget_exposed(w, state->root_widget)) {
      if (auto* elem = get_or_create_element(state, w, -1)) {
        UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(elem),
                                UIA_AutomationFocusChangedEventId);
        elem->Release();
      }
      break;
    }
  }
}

void notify_live_announcement(const std::wstring& message, uint64_t notice_id) {
  if (message.empty() || !UiaClientsAreListening()) return;
  auto fn = resolve_notify_fn();
  if (!fn) return;
  for (auto& state : active_bridges()) {
    if (!state || !state->alive || !state->root_provider) continue;
    BSTR bmsg = SysAllocString(message.c_str());
    std::wstring act = L"clashwin-notice-" + std::to_wstring(notice_id);
    BSTR bact = SysAllocString(act.c_str());
    // NotificationKind_ActionCompleted = 2, NotificationProcessing_ImportantAll = 0
    fn(static_cast<IRawElementProviderSimple*>(state->root_provider), 2, 0,
       bmsg, bact);
    SysFreeString(bmsg);
    SysFreeString(bact);
    break;
  }
}

}  // namespace uia

// ============================================================================
// UiaWindowBridge 实现
// ============================================================================
struct UiaWindowBridge::Impl {
  std::shared_ptr<UiaBridgeState> state;
  UiaRootProvider* root_provider = nullptr;
  bool shut_down = false;
};

UiaWindowBridge::UiaWindowBridge(HWND hwnd, Widget* root,
                                 std::function<float()> dpi_scale_fn)
    : impl_(std::make_unique<Impl>()) {
  auto st = std::make_shared<UiaBridgeState>();
  st->hwnd = hwnd;
  st->root_widget = root;
  st->dpi_scale_fn = std::move(dpi_scale_fn);
  auto* rp = new UiaRootProvider(st);
  st->root_provider = rp;
  impl_->state = st;
  impl_->root_provider = rp;
  active_bridges().push_back(st);
}

UiaWindowBridge::~UiaWindowBridge() { shutdown(); }

void UiaWindowBridge::set_hwnd(HWND hwnd) {
  if (impl_ && impl_->state) {
    impl_->state->hwnd = hwnd;
  }
}

LRESULT UiaWindowBridge::handle_wm_getobject(HWND hwnd, WPARAM wp, LPARAM lp) {
  if (!impl_ || impl_->shut_down || !impl_->state || !impl_->state->alive ||
      !impl_->root_provider) {
    return 0;
  }
  if (hwnd && !impl_->state->hwnd) {
    impl_->state->hwnd = hwnd;
  }
  if (static_cast<long>(lp) == static_cast<long>(UiaRootObjectId) &&
      impl_->state->hwnd) {
    return UiaReturnRawElementProvider(
        impl_->state->hwnd, wp, lp,
        static_cast<IRawElementProviderSimple*>(impl_->root_provider));
  }
  return 0;
}

LRESULT UiaWindowBridge::handle_wm_getobject(WPARAM wp, LPARAM lp) {
  HWND h = (impl_ && impl_->state) ? impl_->state->hwnd : nullptr;
  return handle_wm_getobject(h, wp, lp);
}

void UiaWindowBridge::shutdown() {
  if (!impl_ || impl_->shut_down) return;
  impl_->shut_down = true;

  if (impl_->state) {
    impl_->state->alive = false;
    for (auto& kv : impl_->state->live_providers) {
      for (UiaElementProvider* p : kv.second) {
        if (p) p->detach_widget();
      }
    }
    impl_->state->live_providers.clear();
    if (impl_->state->hwnd) {
      UiaReturnRawElementProvider(impl_->state->hwnd, 0, 0, nullptr);
    }
    auto& bridges = active_bridges();
    bridges.erase(std::remove(bridges.begin(), bridges.end(), impl_->state),
                  bridges.end());
    impl_->state->root_provider = nullptr;
  }

  if (impl_->root_provider) {
    impl_->root_provider->Release();
    impl_->root_provider = nullptr;
  }
}

}  // namespace clashwin::ui

// Widget 基类实现（T10）——见 widget.h 设计说明。
#include "widget.h"

#include <algorithm>
#include <cassert>
#include <format>
#include <typeinfo>

#include "i18n_text.h"
#include "uia_provider.h"
#include "util/log.h"

namespace clashwin::ui {

namespace {
Widget* g_hover = nullptr;
Widget* g_capture = nullptr;
Widget* g_focus = nullptr;
}  // namespace

Widget* hovered_widget() { return g_hover; }
Widget* captured_widget() { return g_capture; }
Widget* focused_widget() { return g_focus; }
void clear_mouse_hover() {
  if (g_hover) {
    Widget* prev = g_hover;
    g_hover = nullptr;
    MouseEvent leave{MouseEventType::Leave, {-1.0f, -1.0f}};
    prev->on_mouse(leave);
  }
}
void clear_input_state() { g_hover = g_capture = g_focus = nullptr; }

// ---------- 树 ----------
Widget::Widget(Widget* parent) : parent_(parent) {}

Widget::~Widget() {
  uia::on_widget_destroyed(this);
  // 自身被直接 delete（绕过 remove_child）时清路由悬空指针
  if (g_hover == this) g_hover = nullptr;
  if (g_capture == this) g_capture = nullptr;
  if (g_focus == this) g_focus = nullptr;
}

void Widget::add_child(std::unique_ptr<Widget> w) {
  // 【所有权约定】w 的所有权移交本树；调用方传入 std::move(x) 后不得再使用 x。
  //   x 为空时此处必为 use-after-move（曾致 AV 0xC0000005），debug 下直接断言。
  assert(w != nullptr && "add_child: 传入空 unique_ptr（典型为 std::move 后复用）");
  w->parent_ = this;
  children_.push_back(std::move(w));
  on_child_added(children_.back().get());
}

void Widget::insert_child(size_t index, std::unique_ptr<Widget> w) {
  assert(w != nullptr && "insert_child: 传入空 unique_ptr（典型为 std::move 后复用）");
  w->parent_ = this;
  index = std::min(index, children_.size());
  children_.insert(children_.begin() + static_cast<long>(index),
                   std::move(w));
  on_child_added(children_[index].get());
}

std::unique_ptr<Widget> Widget::remove_child(Widget* w) {
  for (auto it = children_.begin(); it != children_.end(); ++it) {
    if (it->get() == w) {
      std::unique_ptr<Widget> taken = std::move(*it);
      on_child_removed(w);
      children_.erase(it);
      taken->parent_ = nullptr;
      // 路由指针悬空清理
      if (g_hover == w) g_hover = nullptr;
      if (g_capture == w) g_capture = nullptr;
      if (g_focus == w) g_focus = nullptr;
      return taken;
    }
  }
  return nullptr;
}

// ---------- 几何 ----------
void Widget::set_bounds(const RectF& r) {
  if (bounds_.x == r.x && bounds_.y == r.y && bounds_.w == r.w &&
      bounds_.h == r.h)
    return;
  invalidate();            // 旧区域
  bounds_ = r;
  invalidate();            // 新区域
}

PointF Widget::to_global(PointF local) const {
  PointF g = local;
  for (const Widget* w = this; w; w = w->parent_) {
    g.x += w->bounds_.x;
    g.y += w->bounds_.y;
  }
  return g;
}

PointF Widget::to_local(PointF global) const {
  PointF l = global;
  for (const Widget* w = this; w; w = w->parent_) {
    l.x -= w->bounds_.x;
    l.y -= w->bounds_.y;
  }
  return l;
}

RectF Widget::global_bounds() const {
  RectF g = bounds_;
  for (const Widget* w = parent_; w; w = w->parent_) {
    g.x += w->bounds_.x;
    g.y += w->bounds_.y;
  }
  return g;
}

// ---------- 状态 ----------
void Widget::set_visible(bool v) {
  if (visible_ == v) return;
  bool was = visible_;
  visible_ = v;
  invalidate();
  if (v && !was) {
    // 【LAYOUTFIX】父容器 arrange 跳过不可见子 → 本 widget bounds 从未被布局
    // （0×0）。转为可见时自根重排整树，否则抽屉/覆盖层显示后不可见（无内容）。
    Widget* r = this;
    while (r->parent_) r = r->parent_;
    r->relayout();
  }
  if (!v && g_hover &&
      (g_hover == this || [&] {
         for (Widget* a = g_hover; a; a = a->parent_)  // hover 在本子树内？
           if (a == this) return true;
         return false;
       }())) {
    g_hover = nullptr;
  }
}

void Widget::set_enabled(bool e) {
  if (enabled_ == e) return;
  enabled_ = e;
  invalidate();
}

void Widget::set_opacity(float a) {
  if (a < 0.0f) a = 0.0f;
  if (a > 1.0f) a = 1.0f;
  if (opacity_ == a) return;
  opacity_ = a;
  invalidate();
}

bool Widget::focused() const { return g_focus == this; }

void Widget::request_focus() {
  if (g_focus == this) return;
  Widget* prev = g_focus;
  g_focus = this;
  if (prev) {
    if (prev->on_focus_lost) prev->on_focus_lost();  // MUI onBlur：焦点被接管
    prev->invalidate();
  }
  invalidate();
  uia::on_widget_focused(this);
}
void Widget::blur() {
  if (g_focus == this) {
    g_focus = nullptr;
    if (on_focus_lost) on_focus_lost();  // src: MUI onBlur（失焦提交，:232）
  }
}

// ---------- 布局 ----------
SizeF Widget::measure(float max_w, float max_h) {
  SizeF s = on_measure(max_w, max_h);
  if (s.w <= 0) s.w = 0;
  if (s.h <= 0) s.h = 0;
  return s;
}

void Widget::layout_tree(const RectF& area) {
  set_bounds(area);
  on_arrange(area);
}

void Widget::relayout() {
  // 根快捷方式：整树按当前根 bounds 重排（measure 结果供 arrange 决策）
  on_arrange(bounds_);
}

void Widget::on_arrange(const RectF& area) {
  // 【坐标约定】同 Column/Row/Stack：子 layout_tree 传本 widget 局部坐标
  //（global_bounds 由父链累加；叠加 area.x/y 会使 area≠0 时子件双重偏移
  //  ——traffic Box 的 TrafficPane 曾被偏到窗外致底栏整体不可见）
  RectF content{padding_.l, padding_.t, area.w - padding_.h(),
                area.h - padding_.v()};
  for (auto& c : children_) c->layout_tree(content);
}

// ---------- 绘制 ----------
void Widget::paint(IRenderTarget& rt, const RectF& clip) {
  if (!visible_) return;
  RectF g = global_bounds();
  RectF visible = g.intersect(clip);
  if (visible.is_empty()) return;
  // on_paint 约定局部坐标 {0,0,w,h}：把绘制原点切到本 widget 全局原点
  PointF prev_origin = rt.origin();
  rt.set_origin({g.x, g.y});
  // clip 随 origin 局部化（默认按父区域裁剪允许阴影/位移外溢，clip_to_bounds_ 开启时限制在自身几何）
  if (clip_to_bounds_) {
    rt.push_clip({visible.x - g.x, visible.y - g.y, visible.w, visible.h});
  } else {
    rt.push_clip({clip.x - g.x, clip.y - g.y, clip.w, clip.h});
  }
  const bool faded = opacity_ < 0.999f;
  if (faded) rt.push_opacity(opacity_);
  // ★ ❌8 区域级错误降级：单控件 paint 异常局部降级为占位矩形，避免整树崩溃白屏
  //   （← Mini AreaErrorFallback 区域边界：单组件异常局部降级，严禁整页白屏）。
  //   捕获后继续绘制子控件，并在函数末尾照常 pop origin/clip/opacity。
  try {
    on_paint(rt);
  } catch (const std::exception& e) {
    CWLOG_WARN(std::string("[WIDGET] on_paint 异常已降级为占位: ") +
               (e.what() ? e.what() : "unknown"));
    rt.fill_rect({0, 0, g.w, g.h}, Brush{Color4f(0.9f, 0.3f, 0.2f, 0.12f)});
  } catch (...) {
    CWLOG_WARN("[WIDGET] on_paint 抛出非 std::exception 异常，已降级为占位");
    rt.fill_rect({0, 0, g.w, g.h}, Brush{Color4f(0.9f, 0.3f, 0.2f, 0.12f)});
  }
  // 绘制顺序：z_order 升序稳定（同序按加入顺序 → 后加者在上）
  std::vector<Widget*> ordered;
  ordered.reserve(children_.size());
  for (auto& c : children_) ordered.push_back(c.get());
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const Widget* a, const Widget* b) {
                     return a->z_order_ < b->z_order_;
                   });
  for (Widget* c : ordered) {
    c->paint(rt, visible);
  }
  if (faded) rt.pop_opacity();
  rt.set_origin(prev_origin);
  rt.pop_clip();
}

// ---------- i18n 文本绑定（F-D01） ----------
void Widget::bind_i18n_text(std::wstring* slot, const char* key) {
  if (!slot || !key || !*key) return;
  widget_i18n_rev_ = clashwin::i18n::I18n::instance().revision();
  for (auto& s : i18n_slots_) {
    if (s.slot == slot) {
      s.key = key;
      *slot = tr(key);
      return;
    }
  }
  i18n_slots_.push_back({slot, key});
  *slot = tr(key);
}

void Widget::ensure_i18n_fresh() const {
  const uint64_t rev = clashwin::i18n::I18n::instance().revision();
  if (widget_i18n_rev_ != rev) {
    widget_i18n_rev_ = rev;
    for (const auto& s : i18n_slots_) {
      if (s.slot) *s.slot = tr(s.key.c_str());
    }
  }
}

void Widget::apply_i18n() {
  widget_i18n_rev_ = clashwin::i18n::I18n::instance().revision();
  for (auto& s : i18n_slots_) *s.slot = tr(s.key.c_str());
}

std::wstring Widget::a11y_name() const {
  ensure_i18n_fresh();
  if (!a11y_name_.empty()) return a11y_name_;
  if (a11y_labelled_by_ && a11y_labelled_by_ != this) {
    std::wstring lbl = a11y_labelled_by_->a11y_name();
    if (!lbl.empty()) return lbl;
  }
  std::wstring def = a11y_default_name();
  if (!def.empty()) return def;
  // ← MUI Tooltip: 交互控件无显式文本时自动继承 Tooltip title 作为 aria-label
  const A11yRole role = a11y_role();
  if (role != A11yRole::None && role != A11yRole::Pane &&
      role != A11yRole::Group && role != A11yRole::Window &&
      role != A11yRole::TitleBar) {
    std::wstring tip = tooltip_text();
    if (!tip.empty()) return tip;
  }
  return {};
}

std::wstring Widget::a11y_help_text() const {
  ensure_i18n_fresh();
  std::wstring tip = tooltip_text();
  if (tip.empty()) return {};
  if (tip == a11y_name()) return {};
  return tip;
}

void Widget::refresh_i18n_tree() {
  apply_i18n();
  for (auto& c : children_) c->refresh_i18n_tree();
  std::vector<Widget*> trans;
  collect_transient_children(trans);
  for (Widget* t : trans) {
    if (t) t->refresh_i18n_tree();
  }
}

void Widget::paint_all(IRenderTarget& rt) {
  // ← MF:pages/_layout.tsx:478-483（language 变化触发整树重新翻译）；
  //   C++ 无虚拟 DOM，用 I18n 单调修订号在根节点一次性完成"重取译文 + 重排"。
  //   只在根节点比较，避免每个子控件重复 relayout。
  const uint64_t rev = clashwin::i18n::I18n::instance().revision();
  if (rev != root_i18n_revision_) {
    root_i18n_revision_ = rev;
    refresh_i18n_tree();
    relayout();
  }
  paint(rt, {0, 0, rt.size().w, rt.size().h});
}

// ---------- 命中 ----------
Widget* Widget::hit_test(PointF global) {
  if (!visible_ || !enabled_) return nullptr;
  RectF g = global_bounds();
  if (!g.contains(global.x, global.y)) return nullptr;
  // 与 paint 顺序对偶：绘制序 = z 升序稳定（后者在上），
  // 命中从最上层往回找 = 升序排好后逆序遍历
  std::vector<Widget*> ordered;
  ordered.reserve(children_.size());
  for (auto& c : children_) ordered.push_back(c.get());
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const Widget* a, const Widget* b) {
                     return a->z_order_ < b->z_order_;
                   });
  for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
    if (Widget* hit = (*it)->hit_test(global)) return hit;
  }
  // 子未命中且自身区域命中：仅当点落在自身矩形（容器铺满时恒真）
  if (hit_self(to_local(global))) return this;
  return nullptr;
}

// ---------- 脏区 ----------
void Widget::invalidate() { invalidate({0, 0, bounds_.w, bounds_.h}); }

void Widget::invalidate(const RectF& local) {
  if (!visible_) return;
  FrameClock* fc = frame_clock();
  if (!fc) return;
  RectF g = local;  // 局部矩形 → 全局（沿父链累加原点偏移）
  for (const Widget* w = this; w; w = w->parent_) {
    g.x += w->bounds_.x;
    g.y += w->bounds_.y;
  }
  fc->invalidate(g);
}

// ---------- 路由 ----------
Widget* route_mouse_event(Widget* root, MouseEvent& e) {
  if (!root) return nullptr;

  if (e.type == MouseEventType::Move) {
    Widget* hit = root->hit_test(e.pos);
    if (hit != g_hover) {
      if (g_hover) {
        MouseEvent leave{MouseEventType::Leave, g_hover->to_local(e.pos)};
        g_hover->on_mouse(leave);
      }
      g_hover = hit;
      if (hit) {
        MouseEvent enter{MouseEventType::Enter, hit->to_local(e.pos),
                         e.buttons};
        enter.handled = hit->on_mouse(enter);
      }
    }
    if (!hit) return nullptr;
    MouseEvent local = e;
    local.pos = hit->to_local(e.pos);
    local.handled = hit->on_mouse(local);  // Move 不冒泡
    return local.handled ? hit : nullptr;
  }

  if (e.type == MouseEventType::Up && g_capture) {
    Widget* target = g_capture;
    g_capture = nullptr;
    MouseEvent local = e;
    local.pos = target->to_local(e.pos);
    target->on_mouse(local);
    return target;  // capture 收到 Up 即终止（拖拽移出仍收尾）
  }

  Widget* hit = root->hit_test(e.pos);
  if (!hit) return nullptr;

  // Down/Up/Wheel：命中起沿父链冒泡，第一个处理者赢
  for (Widget* w = hit; w; w = w->parent()) {
    MouseEvent local = e;
    local.pos = w->to_local(e.pos);
    if (w->on_mouse(local)) {
      if (e.type == MouseEventType::Down) g_capture = w;
      return w;
    }
  }
  return nullptr;
}

// ---------- 键盘路由 ----------
namespace {

std::vector<Widget*> ordered_children(Widget* w) {
  std::vector<Widget*> out;
  out.reserve(w->child_count());
  for (size_t i = 0; i < w->child_count(); ++i) out.push_back(w->child(i));
  w->collect_transient_children(out);
  std::stable_sort(out.begin(), out.end(),
                   [](const Widget* a, const Widget* b) {
                     return a->z_order() < b->z_order();
                   });
  return out;
}

Widget* find_topmost_modal(Widget* w) {
  if (!w || !w->a11y_subtree_visible()) return nullptr;
  auto kids = ordered_children(w);
  for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
    if (Widget* m = find_topmost_modal(*it)) return m;
  }
  return w->a11y_is_modal() ? w : nullptr;
}

bool is_in_visible_scope(const Widget* w, const Widget* scope_root) {
  if (!w || !scope_root) return false;
  for (const Widget* cur = w; cur; cur = cur->parent()) {
    if (!cur->a11y_subtree_visible() || !cur->enabled()) return false;
    if (cur == scope_root) return true;
  }
  return false;
}

void collect_focusable_widgets(Widget* w, std::vector<Widget*>& out) {
  if (!w || !w->a11y_subtree_visible() || !w->enabled()) return;
  if (w->a11y_is_focusable()) out.push_back(w);
  for (Widget* c : ordered_children(w)) collect_focusable_widgets(c, out);
}

}  // namespace

Widget* route_key_event(Widget* root, KeyEvent& e) {
  if (!root) return nullptr;
  Widget* scope = find_topmost_modal(root);
  if (!scope) scope = root;

  // Tab / Shift+Tab 无障碍键盘焦点循环（模态开启时自动限制在模态子树内）
  if (e.type == KeyEventType::Down && e.key == VK_TAB && !e.ctrl && !e.alt) {
    std::vector<Widget*> focusables;
    collect_focusable_widgets(scope, focusables);
    if (!focusables.empty()) {
      int cur_idx = -1;
      for (size_t i = 0; i < focusables.size(); ++i) {
        if (focusables[i] == g_focus) {
          cur_idx = static_cast<int>(i);
          break;
        }
      }
      int next_idx = 0;
      const int n = static_cast<int>(focusables.size());
      if (e.shift) {
        next_idx = (cur_idx <= 0) ? (n - 1) : (cur_idx - 1);
      } else {
        next_idx = (cur_idx < 0 || cur_idx + 1 >= n) ? 0 : (cur_idx + 1);
      }
      Widget* target = focusables[static_cast<size_t>(next_idx)];
      target->request_focus();
      e.handled = true;
      return target;
    }
  }

  // 若当前焦点已隐藏/禁用或落在活跃模态外，先释放陈旧焦点（但若无模态且原本在树内则保留）
  if (g_focus && !is_in_visible_scope(g_focus, scope)) {
    g_focus = nullptr;
  }

  if (!g_focus) {
    // 无焦点：找当前作用域内第一个 wants_key 的可见启用 widget（焦点自动落位）
    std::function<Widget*(Widget*)> find = [&](Widget* w) -> Widget* {
      if (!w->visible() || !w->enabled()) return nullptr;
      if (w->wants_key()) return w;
      for (size_t i = 0; i < w->child_count(); ++i)
        if (Widget* r = find(w->child(i))) return r;
      return nullptr;
    };
    g_focus = find(scope);
    if (!g_focus && scope != root && scope->on_key(e)) return scope;
  }
  if (!g_focus) return nullptr;
  if (g_focus->on_key(e)) return g_focus;

  // 非文本输入控件获焦时支持 Space / Enter 无障碍激活或切换
  if (!g_focus->wants_key() && e.type == KeyEventType::Down && !e.ctrl && !e.alt) {
    if (e.key == VK_SPACE) {
      if (g_focus->a11y_toggle_state().has_value() && g_focus->a11y_toggle()) {
        e.handled = true;
        return g_focus;
      }
      if (g_focus->a11y_can_invoke() && g_focus->a11y_invoke()) {
        e.handled = true;
        return g_focus;
      }
    } else if (e.key == VK_RETURN) {
      if (g_focus->a11y_can_invoke() && g_focus->a11y_invoke()) {
        e.handled = true;
        return g_focus;
      }
      if (g_focus->a11y_toggle_state().has_value() && g_focus->a11y_toggle()) {
        e.handled = true;
        return g_focus;
      }
    }
  }
  return nullptr;
}

}  // namespace clashwin::ui

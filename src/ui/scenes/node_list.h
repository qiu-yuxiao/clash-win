#pragma once
// 节点列表（T13）：过滤工具条 35px（⚡测速 26×26 + 3 态排序 26×26 + 搜索框内 ✕）。
// src: proxy-head.tsx:69-159（⚡ :81-107 / 排序 :109-140 / 搜索 :142-158）
// 行布局 ← proxy-item.tsx：
//   行高 26（:67 minHeight 24 + :75 borderBottom 2px）；左 55px 类型
//   （:106 width 55；11px 灰大写右对齐、右边框 2px divider）
//   + 名称 flex（12px、px=8、selected bold、右边框 2px divider）
//   + 右 65px 延迟（:200 width 65；11px bold600 居中、五档色 ← formatDelayColor）
//   选中：左边框 3px primary + bg alpha(primary,0.15 light/0.35 dark)；
//   奇偶行 rgba(0,120,215,.04) light；底边框 2px divider。
// 无分组头（Mini 单组扁平列表，src: use-render-list.ts:144）；
// 空态：Inbox 图标 2.5em + "No Proxies" 居中（src: proxy-render.tsx:63-78）。
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../container.h"
#include "../icon_w.h"
#include "../label.h"
#include "../scroll_list.h"
#include "../textfield.h"
#include "../widget.h"
#include "node_data.h"

namespace clashwin::ui::scene {

inline constexpr float kNodeRowH = 26;       // src: proxy-item.tsx:67 minHeight 24 + :75 borderBottom 2px = 26
inline constexpr float kFilterBarH = 35;     // src: proxy-head.tsx:70-79 py4+26+pb4+border1
inline constexpr float kListTopGap = 4;      // src: proxy-head.tsx:77 mb 0.5=4
inline constexpr float kNodeTypeColW = 55;   // src: proxy-item.tsx:106 width 55
inline constexpr float kNodeDelayColW = 65;  // src: proxy-item.tsx:200 width 65
inline constexpr float kListInset = 8;       // src: proxy-groups.tsx:327 mx: 1 → 8
// theme-panel 的「border: 4px double」（src: index.scss:75/125）在 border-box 下向内占 4px：
// 过滤栏与列表都是它的子节点，必须整体内缩，否则边框圈不到过滤栏（见 on_arrange）。
inline constexpr float kPanelBorder = 4;
inline constexpr float kListBottomGap = 8;   // src: proxy-groups.tsx:327 mb: 1 → 8
inline constexpr float kNodeBorderW = 2;     // src: proxy-item.tsx:75 borderBottom 2px
inline constexpr float kNodeSelectBorderW = 3;  // src: proxy-item.tsx 选中 borderLeft 3px
// 滚动置顶按钮几何 ← scroll-top-button.tsx:18-19 / proxy-groups.tsx:171
inline constexpr float kScrollTopBtnSize = 40;   // IconButton padding 8×2 + icon 24
inline constexpr float kScrollTopInset = 20;     // bottom/right 20px
inline constexpr float kScrollTopShowPx = 100;   // proxy-groups.tsx:172 scrollTop > 100

// 五档延迟色（← formatDelayColor → MUI 色名 → defaultTheme 色值）
Color4f delay_tier_color(DelayTier tier);

// 单行（节点行）
class NodeRowWidget : public Widget {
 public:
  void bind(const std::string& type, const std::string& name, int delay,
            bool selected);
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;
  std::function<void()> on_click;
  int index_in_group = 0;  // 奇偶行底色

 private:
  std::string type_, name_;
  int delay_ = -1;
  bool selected_ = false;
  bool hover_ = false;
};

// 滚动置顶按钮 ← src/components/layout/scroll-top-button.tsx:10-40
//   · position absolute / bottom 20 / right 20（相对整个节点列容器外框）
//   · MUI IconButton 默认尺寸：padding 8 + 图标 24 → 40×40 圆形（borderRadius 50%）
//   · 底色 light rgba(0,0,0,.1) / dark rgba(255,255,255,.1)，hover 各 .2
//   · <Fade in={show}>：MUI 默认 enteringScreen 225ms / leavingScreen 195ms
//   · 图标 KeyboardArrowUp；显隐阈值 scrollTop > 100（proxy-groups.tsx:172）
class ScrollTopButton : public Widget {
 public:
  explicit ScrollTopButton(Widget* parent = nullptr);

  // 显隐（等价 <Fade in=…>）：切换时启动淡入/淡出
  void set_shown(bool show);
  bool shown() const { return shown_; }
  // 淡出完成前仍参与绘制；alpha==0 时不可点击（MUI Fade 退出后 visibility:hidden）
  bool interactive() const { return alpha_ > 0.0f; }
  float alpha() const { return alpha_; }

  std::function<void()> on_click;

  SizeF on_measure(float max_w, float max_h) override;
  void on_arrange(const RectF& area) override;
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;
  // MUI <Fade> 退出后 visibility:hidden → 不参与命中（点击穿透回列表）
  Widget* hit_test(PointF global) override;

 private:
  void advance_anim(int64_t now_ms);

  IconW* icon_ = nullptr;
  bool shown_ = false;
  bool hover_ = false;
  float alpha_ = 0.0f;
  float anim_from_ = 0.0f;
  float anim_to_ = 0.0f;
  int64_t anim_start_ms_ = 0;
  int64_t anim_dur_ms_ = 0;
};

class NodeList : public Widget {
 public:
  explicit NodeList(Widget* parent = nullptr);
  ~NodeList() override;

  void bind(NodeData* data);          // 数据源（不拥有）

  // 过滤（搜索框；name 子串大小写不敏感）
  void set_filter(const std::string& text);
  // 排序切换（3 态循环：原始序 / 延迟升序 / 名称 locale 升序）
  // src: proxy-head.tsx:126-128 sortType=(sortType+1)%3
  void toggle_sort();
  bool sorted_by_delay() const { return sort_type_ == 1; }

  // 过滤后的行数（测试断言）
  size_t visible_count() const { return visible_.size(); }
  // 空态是否在显示（测试断言）
  bool empty_state_visible() const;
  ScrollList* list() const { return list_; }
  TextField* search_box() const { return search_; }
  ScrollTopButton* scroll_top_button() const { return scroll_top_; }
  // ★ §5.2/§5.6 闪电（批量测速）按钮（测试断言用；点击语义见 node_list.cpp）
  //   ← Mini proxy-head.tsx:81-107 BoltOutlined → proxy-groups.tsx:230 handleCheckAll
  IconW* bolt_button() const { return bolt_btn_; }

  // 皮肤/亮暗热切换：把控件级缓存（输入框查表结果）重刷为当前皮肤值
  // （TextField::apply_theme 只在构造时调一次，切皮肤后缓存仍是旧皮肤的）
  void restyle_theme();

  // 点击节点回调（NodeData::select 由宿主接，失败回弹由 NodeList 处理：
  // select 失败时不更新选中态）
  std::function<bool(const std::string& node)> select_node;  // 返回 false 回弹

  void on_paint(IRenderTarget& rt) override;
  void on_arrange(const RectF& area) override;

 private:
  void rebuild_visible();   // 过滤+排序 → visible_ 索引
  void sync_list();         // ScrollList 内容重建

  NodeData* data_ = nullptr;
  ScrollList* list_ = nullptr;
  TextField* search_ = nullptr;
  Widget* search_wrap_ = nullptr;  // src: proxy-head.tsx:142-148 ml4 + flex1 外层
  IconW* sort_btn_ = nullptr;   // src: proxy-head.tsx:109-140 3 态排序钮
  IconW* bolt_btn_ = nullptr;   // src: proxy-head.tsx:81-107 ⚡ 批量测速钮
  IconW* empty_icon_ = nullptr;   // src: proxy-render.tsx:75 InboxRounded 2.5em
  Label* empty_label_ = nullptr;  // src: proxy-render.tsx:76 "No Proxies"
  ScrollTopButton* scroll_top_ = nullptr;  // ← scroll-top-button.tsx（proxy-groups.tsx:330）
  std::string filter_;
  int sort_type_ = 1;  // src: use-head-state.ts:21 sortType 初值 1（按延迟）
  // visible_：rows_ 索引（无分组头）
  std::vector<int> visible_;
};

}  // namespace clashwin::ui::scene

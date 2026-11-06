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
inline constexpr float kListPadTop = 4;      // src: proxy-head.tsx:77 mb 0.5(=4)
inline constexpr float kNodeBorderW = 2;     // src: proxy-item.tsx:75 borderBottom 2px
inline constexpr float kNodeSelectBorderW = 3;  // src: proxy-item.tsx 选中 borderLeft 3px

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
  IconW* empty_icon_ = nullptr;   // src: proxy-render.tsx:75 InboxRounded 2.5em
  Label* empty_label_ = nullptr;  // src: proxy-render.tsx:76 "No Proxies"
  std::string filter_;
  int sort_type_ = 1;  // src: use-head-state.ts:21 sortType 初值 1（按延迟）
  // visible_：rows_ 索引（无分组头）
  std::vector<int> visible_;
};

}  // namespace clashwin::ui::scene

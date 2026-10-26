#pragma once
// 节点列表（T13）：过滤工具条 34px（⚡测速 + ↓排序 + 搜索框）+ 虚拟滚动列表。
// ← census §2/T13 卡 + clashwin_pixel_perfect.png + proxy-item.tsx 行布局：
//   行高 39；窄窗行 = 左 55px 类型（11px 灰大写右对齐、右边框 2px divider）
//   + 名称 flex（12px、px=8、selected bold、右边框 2px divider）
//   + 右 65px 延迟（11px bold600 居中、五档色 ← formatDelayColor 250/400/2000）
//   选中：左边框 3px primary + bg alpha(primary,0.15 light/0.35 dark)；
//   奇偶行 rgba(0,120,215,.04) light；底边框 2px divider。
// 分组头行：组名（VMESS）+ 当前节点（primary 橙字、选中橙浅底）。
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

inline constexpr float kNodeRowH = 39;       // ← census 行高 39
inline constexpr float kFilterBarH = 34;     // ← T13 卡过滤工具条 34px
inline constexpr float kNodeTypeColW = 55;   // ← proxy-item isMinimal 左列
inline constexpr float kNodeDelayColW = 65;  // ← 右延迟列
inline constexpr float kNodeBorderW = 2;     // ← 行内右边框/底边框 2px
inline constexpr float kNodeSelectBorderW = 3;  // ← 选中 borderLeft 3px

// 五档延迟色（← formatDelayColor → MUI 色名 → defaultTheme 色值）
Color4f delay_tier_color(DelayTier tier);

// 单行（分组头或节点行）
class NodeRowWidget : public Widget {
 public:
  // is_group_head：分组头行（组名+当前节点，橙浅底橙字）
  void bind(bool is_group_head, const std::string& type, const std::string& name,
            int delay, bool selected);
  void on_paint(IRenderTarget& rt) override;
  bool on_mouse(const MouseEvent& e) override;
  std::function<void()> on_click;
  int index_in_group = 0;  // 奇偶行底色

 private:
  bool group_head_ = false;
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
  void set_group_title(const std::wstring& title);  // 组名显示（如 PROXY/VMESS）

  // 过滤（搜索框；name 子串大小写不敏感）
  void set_filter(const std::string& text);
  // 排序切换（默认=组序 / 延迟升序、Unknown 沉底）
  void toggle_sort();
  bool sorted_by_delay() const { return sort_by_delay_; }

  // 过滤后的行数（测试断言）
  size_t visible_count() const { return visible_.size(); }
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
  // i=-1 → 分组头行；>=0 → rows_ 索引
  int visible_row(size_t i) const { return i < visible_.size() ? visible_[i] : -2; }

  NodeData* data_ = nullptr;
  ScrollList* list_ = nullptr;
  TextField* search_ = nullptr;
  std::wstring group_title_ = L"PROXY";
  std::string filter_;
  bool sort_by_delay_ = false;
  // visible_：i<0 → -1 表示分组头；>=0 → rows_ 索引
  std::vector<int> visible_;
};

}  // namespace clashwin::ui::scene

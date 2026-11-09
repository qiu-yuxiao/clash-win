// §5.2/§5.6「所见即所选」：批量测速的子集约束与择优切换
//
// 对照源 = ClashVerge src/components/proxy/proxy-groups.tsx:230-275
//   handleCheckAll(groupName)：
//     ① 从**当前过滤后可见**的渲染列表提取节点名（剔除 dummy）
//     ② 视觉占位：逐节点 setDelay(name, group, -2)（触发流光动画）
//     ③ 委托后端统一引擎 triggerAutoSelect(currentUid, visibleNames, 0, true)
//        —— 关键：只在该子集内测速并挑最快，**而非 PROXY 全量**
//
// 与「全量自动优选」（不传子集）行为区分：所见即所测所选。
#include <windows.h>

#include <string>
#include <vector>

#include "test_ui_helper.h"
#include "test_util.h"

#include "ui/scenes/node_data.h"
#include "ui/scenes/node_list.h"

using namespace clashwin::ui;
using namespace clashwin::ui::scene;
using namespace ui_test;

namespace {

struct MockState {
  std::vector<std::string> all = {"n1", "n2", "n3"};
  std::string now = "n1";
  std::map<std::string, std::string> types = {{"n1", "VMESS"},
                                              {"n2", "HYSTERIA2"},
                                              {"n3", "ANYTLS"}};
  // ★ 记录 test_and_select 收到的子集
  std::vector<std::string> last_subset;
  int test_and_select_calls = 0;
  bool test_and_select_ok = true;
  int64_t clock_ms = 1000;
};

NodeData make_data(MockState& st) {
  DataHooks h;
  h.fetch_group = [&st](std::vector<std::string>* all, std::string* now) {
    *all = st.all;
    *now = st.now;
    return true;
  };
  h.fetch_proxies = [&st](std::map<std::string, clashwin::mihomo::ProxyItem>* items) {
    for (const auto& [name, type] : st.types) {
      clashwin::mihomo::ProxyItem it;
      it.name = name;
      it.type = type;
      // 夹具语义 = 存活但未测（mihomo 对未测节点返回 alive=true）。
      // v0.2.2 ❌2 起 alive=false 会被 row_delay 判成 Timeout(0)，与本文件
      // 「未测 = -1」的断言冲突，故显式置 true。
      it.alive = true;
      (*items)[name] = it;
    }
    return true;
  };
  h.select_node = [](const std::string&) { return true; };
  h.test_group = [](const std::string&) { return true; };
  h.test_and_select = [&st](const std::vector<std::string>& nodes) -> std::string {
    ++st.test_and_select_calls;
    st.last_subset = nodes;
    return st.test_and_select_ok ? (nodes.empty() ? "n1" : nodes[0]) : "";
  };
  h.now_ms = [&st] { return st.clock_ms; };
  return NodeData(std::move(h));
}

}  // namespace

// 1) 闪电按钮必须把「可见子集」传给后端，而不是全组
//    ← 走真实点击路径（NodeList::bolt_button()->on_click）
UT_TEST(t28_bolt_passes_visible_subset) {
  MockState st;
  NodeData data = make_data(st);
  UT_CHECK(data.load());

  NodeList list;
  list.bind(&data);
  list.layout_tree({0, 0, 271, 400});
  list.set_filter("");  // 无过滤 → 可见子集 = 全部 3 个
  UT_CHECK_EQ(list.visible_count(), 3u);

  auto* bolt = list.bolt_button();
  UT_CHECK(bolt != nullptr);
  if (!bolt) return;
  UT_CHECK(bolt->on_click != nullptr);
  bolt->on_click();
  for (int i = 0; i < 100 && st.test_and_select_calls == 0; ++i) {
    ::Sleep(10);
  }

  UT_CHECK_EQ(st.test_and_select_calls, 1);
  UT_CHECK_EQ(st.last_subset.size(), 3u);
  UT_CHECK(st.last_subset[0] == "n1");
  UT_CHECK(st.last_subset[1] == "n2");
  UT_CHECK(st.last_subset[2] == "n3");
}

// 1b) 过滤生效 → 可见子集只含过滤命中的节点（防越界关键）
UT_TEST(t28_bolt_subset_respects_filter) {
  MockState st;
  NodeData data = make_data(st);
  UT_CHECK(data.load());

  NodeList list;
  list.bind(&data);
  list.layout_tree({0, 0, 271, 400});
  list.set_filter("n2");
  UT_CHECK_EQ(list.visible_count(), 1u);

  auto* bolt = list.bolt_button();
  UT_CHECK(bolt != nullptr);
  if (!bolt) return;
  bolt->on_click();
  for (int i = 0; i < 100 && st.test_and_select_calls == 0; ++i) {
    ::Sleep(10);
  }

  UT_CHECK_EQ(st.test_and_select_calls, 1);
  UT_CHECK_EQ(st.last_subset.size(), 1u);
  UT_CHECK(st.last_subset[0] == "n2");
}

// 1c) 空子集 → 传全量（与自动优选同语义）
UT_TEST(t28_bolt_empty_subset_means_full) {
  MockState st;
  NodeData data = make_data(st);
  UT_CHECK(data.load());
  std::vector<std::string> empty;
  data.test_and_select(empty);
  UT_CHECK_EQ(st.test_and_select_calls, 1);
  UT_CHECK(st.last_subset.empty());
}

// 3) 视觉占位：调用后待测节点延迟置 -2（testing），驱动流光动画
//    ← Mini proxy-groups.tsx:253-255 setDelay(name, groupName, -2)
UT_TEST(t28_bolt_marks_testing_placeholder) {
  MockState st;
  NodeData data = make_data(st);
  UT_CHECK(data.load());
  UT_CHECK_EQ(data.row_delay("n1"), -1);  // 测速前：未测

  data.test_and_select({"n1", "n2"});
  UT_CHECK_EQ(data.row_delay("n1"), -2);  // testing 中间态
  UT_CHECK_EQ(data.row_delay("n2"), -2);
  UT_CHECK_EQ(data.row_delay("n3"), -1);  // 不在子集内 → 不变
}

// 4) 后端未执行（busy/失败）→ 返回 false，且不得把节点标成 Timeout
//    （§5.2 契约第 3 条：测速未执行严禁伪造成 Timeout）
UT_TEST(t28_bolt_failure_does_not_fake_timeout) {
  MockState st;
  st.test_and_select_ok = false;
  NodeData data = make_data(st);
  UT_CHECK(data.load());
  data.test_and_select({"n1"});
  UT_CHECK_EQ(st.test_and_select_calls, 1);
  // 仍是 testing(-2) 或未测(-1)，绝不是 0（Timeout）
  const int d = data.row_delay("n1");
  UT_CHECK(d == -2 || d == -1);
  UT_CHECK(d != 0);
}

// 5) 过滤生效时，可见子集只含过滤命中的节点（见 1b，此处保留直调路径断言）
UT_TEST(t28_bolt_subset_direct_call) {
  MockState st;
  NodeData data = make_data(st);
  UT_CHECK(data.load());

  std::vector<std::string> names;
  for (const auto& r : data.rows()) {
    if (r.name.find("n2") != std::string::npos) names.push_back(r.name);
  }
  data.test_and_select(names);
  UT_CHECK_EQ(st.last_subset.size(), 1u);
  UT_CHECK(st.last_subset[0] == "n2");
}

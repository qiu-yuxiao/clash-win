#pragma once
// Modal（T10）：模态遮罩 + 居中对话框底座（标题/内容/按钮排）。
// 对照 MUI Dialog：全屏半透明遮罩吞掉下层全部点击（z_order=1000 顶层），
// 点遮罩默认关闭（on_mask_click 可拦截）；按钮排 [取消, 确定] 左→右。
#include <functional>
#include <string>
#include <vector>

#include "container.h"
#include "label.h"

namespace clashwin::ui {

class Modal : public Widget {
 public:
  explicit Modal(Widget* parent = nullptr) : Widget(parent) {
    set_z_order(1000);
    build_tree();
  }

  std::function<void(int)> on_button;         // 按钮 index（0=取消…末位=确定）
  std::function<bool()> on_mask_click;        // 返回 true 拦截默认关闭
  std::function<void()> on_open, on_close;

  bool is_open() const { return open_; }

  // 打开：content 所有权转移给对话框体；dialog_size 决定对话框尺寸
  void open(std::unique_ptr<Widget> content, const SizeF& dialog_size,
            const std::wstring& title = L"");
  void close();
  void set_buttons(std::vector<std::wstring> labels);  // 缺省 [取消, 确定]

  const std::wstring& title() const { return title_; }
  Widget* content() const { return content_; }
  RectF dialog_bounds() const;

  // 遮罩吞下层点击
  bool on_mouse(const MouseEvent& e) override;
  void on_paint(IRenderTarget& rt) override;
  void on_arrange(const RectF& area) override;
  // 未打开时不参与命中（下层可交互）
  bool hit_self(PointF local) const override {
    return open_ && Widget::hit_self(local);
  }

  // 绘制参数
  Color4f mask_ = Color4f::hex(0x000000, 0.50f);  // src: MUI Dialog backdrop rgba(0,0,0,.5)
  Color4f dialog_bg_ = Color4f::hex(0xFFFFFF);  // src: MUI background.paper #fff
  Color4f title_text_ = Color4f::hex(0x000000, 0.87f);  // src: MUI text.primary
  float radius_ = 8;  // ← MUI Dialog paper 圆角 8

 private:
  void build_tree();  // dialog Column: title_row / content / buttons_row
  void layout_dialog();

  bool open_ = false;
  std::wstring title_;
  Widget* content_ = nullptr;        // add_child 转移所有权
  SizeF dialog_size_{};
  Column* dialog_ = nullptr;         // 子（dialog Column）
  Label* title_label_ = nullptr;     // 子
  Row* buttons_row_ = nullptr;       // 子
  std::vector<std::wstring> buttons_ = {L"取消", L"确定"};
  // F-D01：按钮文案的 Mini locale key（← shared.actions.cancel / confirm）。
  //   open() 时按当前语言重取，set_buttons() 自定义标签后失效。
  std::vector<std::string> button_keys_ = {"shared.actions.cancel",
                                           "shared.actions.confirm"};
};

}  // namespace clashwin::ui

#pragma once
// ThemeService 拆分单元共享的内部颜色辅助函数。

#include "ui/d2d_context.h"

namespace clashwin::ui::theme::detail {

inline Color4f with_alpha(const Color4f& c, float a) { return {c.r, c.g, c.b, a}; }

}  // namespace clashwin::ui::theme::detail

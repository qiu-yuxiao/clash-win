#pragma once
// 实用工具弹窗族伞状聚合头文件（向后兼容所有既有 #include "scenes/utility_modals.h" 调用点）。
// 各弹窗已按职责拆分为独立文件：src/ui/scenes/dialogs/*.{h,cpp}。
// src: src/pages/_layout/components/layout-dialogs.tsx:1-120

#include "ui/scenes/dialogs/client_update_modal.h"
#include "ui/scenes/dialogs/core_update_modal.h"
#include "ui/scenes/dialogs/delete_profile_modal.h"
#include "ui/scenes/dialogs/edit_profile_modal.h"
#include "ui/scenes/dialogs/log_viewer_modal.h"

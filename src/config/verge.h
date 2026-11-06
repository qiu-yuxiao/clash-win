#pragma once
// IVerge 读写（verge.yaml）—— 对应 ClashVerge config/verge.rs 的持久化部分
// bitflags 联动（feat/config.rs patch_verge 的副作用）属 T09，此处只提供
// 「读-改-存」原语。模板合并/迁移在 config.cpp 的 Config 装配层完成。
#include "clashwin/config/models.h"

namespace clashwin::config::verge {

// ← verge.rs::new：读 verge.yaml（缺失/损坏 → template()）；
// 版本迁移由 migrate 层负责（Config::init 统一编排），此处仅原样读取
Result<IVerge> load();

// ← verge.rs::template()：出厂默认（language 取系统语言，对照
// clash_verge_i18n::system_language：zh*→zh、zh-tw/hk/mo/cht→zhtw、ja*→jp、
// en*→en，其余回退 zh）
IVerge make_template();

// 系统语言探测（供模板与 T10 i18n 复用）
std::string system_language();

// 安装器语言 → 应用语言（用户指令 2026-11-07）。
// NSIS 安装器在开头弹语言选择（clashwin.nsi .onInit 的 MUI_LANGDLL_DISPLAY），
// MUI 把所选语言 ID 写在 HKCU\Software\Clash Win\ClashWin 的
// "Installer Language"（REG_SZ 数字串）。应用首次生成 verge.yaml 时优先采用它，
// 取不到才回落系统语言 —— 用户口径：「在安装器里选了中文，接下来应用界面
// 就该是中文」。None 返回空串，由调用方回落。
std::string installer_language();
// 纯映射（可单测）：2052 SimpChinese→zh / 1033 English→en / 1049 Russian→ru，
// 其余（含 0）→ 空串。
std::string language_from_installer_id(int id);

// 序列化（serde_yaml_ng 逐字段形态；测试用于模板 diff 基准）
std::string dump_verge(const IVerge& v);

// save_file 的通用落盘（前缀 "# Clash Verge Config"）
Result<void> save_verge(const IVerge& v);

// verge.yaml 路径（<home>\verge.yaml）
std::wstring verge_path();

}  // namespace clashwin::config::verge

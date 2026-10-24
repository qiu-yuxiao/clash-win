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

// 序列化（serde_yaml_ng 逐字段形态；测试用于模板 diff 基准）
std::string dump_verge(const IVerge& v);

// save_file 的通用落盘（前缀 "# Clash Verge Config"）
Result<void> save_verge(const IVerge& v);

// verge.yaml 路径（<home>\verge.yaml）
std::wstring verge_path();

}  // namespace clashwin::config::verge

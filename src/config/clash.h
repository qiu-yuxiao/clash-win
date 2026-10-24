#pragma once
// IClashTemp 实现（config.yaml）—— 对应 ClashVerge config/clash.rs
#include "clashwin/config/models.h"

namespace clashwin::config::clash {

// ← clash.rs::new：读 config.yaml →（版本检测先于模板合并）→ 补模板缺失键 →
//   备份+迁移 → secret 修正 → controller 冲突自动寻位 → guard
IClashTemp load();

// ← clash.rs::template()：全部出厂键值，插入序照抄（决定产物字节序）
IClashTemp make_template();

// 序列化（serde 布局；供测试 diff）
std::string dump_clash(const IClashTemp& c);

Result<void> save_clash(const IClashTemp& c);

std::wstring clash_path();

}  // namespace clashwin::config::clash

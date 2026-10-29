#pragma once
// T25 预置离线资源包释放与版本时间戳对齐（← utils/init.rs init_resources）
// 预置文件：
//   1. Country.mmdb -> <home>/Country.mmdb
//   2. geosite.dat -> <home>/geosite.dat
//   3. gfwlist.yaml -> <home>/ruleset/gfwlist.yaml (BUG-309 0ms 冷启动防直连被墙)
// 覆盖规则：
//   - 若目标不存在或大小为0字节：直接复制
//   - 若目标已存在且大小>0：仅当源文件修改时间新于目标文件（src_time > dest_time）时才覆盖
//     （若运行期内核自动拉取了更新版本，dest_time > src_time，保留更新版）
#include <string>
#include "util/result.h"

namespace clashwin::system {

Result<void> init_resources(const std::wstring& app_home_dir,
                            const std::wstring& app_resources_dir);

}  // namespace clashwin::system

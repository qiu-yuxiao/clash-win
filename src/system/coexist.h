#pragma once
// ★ Mini/Win 互斥共存方针（用户 2026-11-09 改定；总纲《移植基本准则.md》§5.2）
// ★ 「借用与归还」裁决（用户 2026-11-10，总纲 §5.2.2）：
//
// - Clash Mini 与 Clash Win 可同时安装于同一台机器，但绝对避免同时运行；
// - 一方启动/安装时，强杀另一方的所有进程（含服务运行实例），但不删除对方磁盘上
//   的任何东西，也不碰对方的注册表（含自启值）；
// - 服务名固定 clash_verge_service（两侧同名，改不了）。服务二进制继承自上游
//   Clash Verge，是同一个组件：注册在位且二进制哈希与上游基线一致 → 共用不夺注册
//   （零 UAC）；仅缺失/损坏/哈希不一致才重装接管；卸载时归还给仍在装的对方；
// - 归属判定按 exe 安装目录路径，绝不按进程名（ClashWin 内核=win-mihomo.exe、Clash Mini 内核=mini-mihomo.exe，已不再同名；按名杀
//   会误杀自家内核）；绝不发 WM_CLOSE（Mini 收到关闭只会进轻量模式隐藏，不会退出）。
//
// 取代旧「共存豁免 + refuse_service_mutation」设计与「安装即夺注册」细则。
#include <string>
#include <string_view>

namespace clashwin::system::coexist {

// 路径归属判定：exe_path 是否位于本方目录（exe 目录 / app_home，含子目录）内。
// 大小写不敏感；空路径 → false。供进程分类与服务接管判定共用（纯函数可单测）。
bool is_own_path(const std::wstring& exe_path);

// 已注册服务的 ImagePath 可执行文件是否指向本方目录。
// 服务缺失 / 解析失败 → false（宁可误判为对方再走接管路径，也不误判为本方）。
bool service_image_is_own();

// 已注册服务的 ImagePath 二进制 SHA256 是否与上游基线（kServiceExeSha256）一致。
// 总纲 §5.2.2：一致 → 同一个上游组件，共用不夺注册；服务缺失/文件不在/哈希不符 → false。
bool service_image_matches_upstream();

// 纯函数： lowercase hex SHA256 与基线比较（大小写不敏感），供单测锁定。
bool upstream_baseline_equals(std::string_view hex_lower_or_mixed);

// 启动/安装互斥：强杀所有「对方」进程（按路径归属判定），返回成功终止的进程数。
// 分类规则：exe 路径不属本方，且文件名属于两 App 共享组件名集合
//   （clash-mini / mini-mihomo / win-mihomo / clash-verge-service* / enableLoopback / clash-verge），
//   或其目录片段含 "clash mini" / "clash-mini"。自身 PID 与系统 PID 永不进入候选。
int kill_other_processes();

// 杀对方后的收尾：清理对方遗留的 Wintun/Mihomo 虚拟网卡（孤儿网卡），
// 避免本方随后开 TUN 时同名网卡冲突导致全流量接管断网（0.2.3 的 TUN 回归根因）。
// 仅在本进程已提权时执行（TUN 必走提权服务；启动期未提权则跳过，与「启动期禁止 UAC」
// 方针一致；TUN 接管路径必提权，届时自然清掉）。不碰对方任何磁盘文件。
void remove_orphan_wintun_adapters();

// 若服务已注册、正在运行且 ImagePath 指向对方路径 → ControlService 停止（不删注册）。
// 无权限时静默跳过（启动期禁止 UAC 弹窗；接管留给 install_service 的 UAC 路径）。
// 返回是否执行了停止动作。
bool stop_foreign_service_if_running();

// 删除 clash_verge_service 注册（仅注册表，不动任何磁盘文件）。
// DeleteService 被拒（非管理员）→ runas 提权 sc.exe delete 回退（UAC）。
// 供 install_service 接管路径调用；服务本就不存在 → 直接 true。
bool delete_service_registration();

}  // namespace clashwin::system::coexist

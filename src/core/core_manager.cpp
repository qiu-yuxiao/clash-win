// T07 内核生命周期 · Sidecar 模式 + 重启恢复 + 热应用
// 对照源：ClashVerge src-tauri/src/core/manager/{mod,state,lifecycle,config}.rs
#include "core/core_manager.h"

#include <windows.h>
#include <tlhelp32.h>

#include "config/config.h"
#include "config/proxy_head_state.h"
#include "core/service_mode.h"
#include "mihomo/service_ipc.h"
#include "system/service_runner.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/node.h"
#include "util/utf.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace clashwin::core {

namespace fs = std::filesystem;
using clashwin::mihomo::BaseConfig;
using clashwin::mihomo::MihomoClient;
using clashwin::mihomo::ProxyItem;

namespace {

// 进程环境缺省变量解析（← state.rs:39-41 std::env::var unwrap_or）
std::string env_or_default(const char* name, const char* def) {
  char buf[256];
  size_t n = 0;
  if (!getenv_s(&n, buf, sizeof(buf), name) && n > 0) return buf;
  return def;
}

// 构造 Windows 环境块：当前环境 + 覆写三个 Go 变量（对齐 tauri .env() 叠加语义）。
// 注意：CreateProcessW 要求环境块按变量名（不区分大小写）字母序排列，否则行为未定义
// （实测可致 ERROR_INVALID_PARAMETER 87）。
}  // namespace

// （公开函数，见 core_manager.h）
std::wstring build_sidecar_env_block(const SidecarLaunch& s) {
  std::vector<std::wstring> entries;
  // 1) 复制当前环境（跳过同名三变量，下方统一以新值覆盖）
  LPWCH envs = ::GetEnvironmentStringsW();
  if (envs) {
    for (LPWCH p = envs; *p;) {
      std::wstring entry(p);
      bool is_go = false;
      for (const wchar_t* key : {L"GOMEMLIMIT", L"GOGC", L"GOMAXPROCS"}) {
        size_t l = wcslen(key);
        if (entry.size() > l && entry.compare(0, l, key) == 0 && entry[l] == L'=') {
          is_go = true;
          break;
        }
      }
      // "=" 开头的保留项（驱动器当前目录设置）传入 lpEnvironment 会触发
      // ERROR_INVALID_PARAMETER(87)，构造自定义环境块时必须剔除
      // 先取长度再移动：move 后 entry.size() 未指定（实测 0），曾致环境块步进错位
      const size_t entry_len = entry.size();
      if (!is_go && !entry.empty() && entry[0] != L'=') entries.push_back(std::move(entry));
      p += entry_len + 1;
    }
    ::FreeEnvironmentStringsW(envs);
  }
  // 2) 追加 Go 三变量（utf16 转换失败丢弃该项）
  auto append_env = [&entries](const char* key, const std::string& value) {
    auto k = utf::utf8_to_utf16(key);
    auto v = utf::utf8_to_utf16(value);
    if (k.empty() || v.empty()) return;
    entries.push_back(k + L"=" + v);
  };
  append_env("GOMEMLIMIT", s.go_memlimit);
  append_env("GOGC", s.go_gc);
  append_env("GOMAXPROCS", s.go_maxprocs);
  // 3) 按名排序（不区分大小写，MSDN 要求）后拼接
  std::sort(entries.begin(), entries.end(), [](const std::wstring& a, const std::wstring& b) {
    auto key_of = [](const std::wstring& e) {
      size_t eq = e.find(L'=');
      std::wstring k = e.substr(0, eq == std::wstring::npos ? e.size() : eq);
      std::transform(k.begin(), k.end(), k.begin(), ::towlower);
      return k;
    };
    return key_of(a) < key_of(b);
  });
  std::wstring block;
  for (const auto& e : entries) {
    block.append(e);
    block.push_back(L'\0');
  }
  if (block.empty()) block.push_back(L'\0');
  block.push_back(L'\0');
  return block;
}

// 日志消费线程（← state.rs:89-119：stdout=Info / stderr=Warn；终止行 + code==0 清日志语义
// 以文件追加 + 终止标记实现）。thread_count 传入 1/2，线程自己从共享计数认领管道。
namespace {

void log_consumer_thread(HANDLE read_end, const std::wstring& log_path,
                         std::atomic<bool>* run_flag) {
  std::string leftover;
  // 打开日志文件（共享写追加；两个线程各持句柄，OS 保证追加原子性足够）
  while (run_flag->load()) {
    char buf[4096];
    DWORD got = 0;
    if (!::ReadFile(read_end, buf, sizeof(buf), &got, nullptr) || got == 0) break;
    leftover.append(buf, got);
    size_t pos;
    while ((pos = leftover.find('\n')) != std::string::npos) {
      std::string line = leftover.substr(0, pos);
      leftover.erase(0, pos + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty()) continue;
      std::ofstream f(log_path, std::ios::binary | std::ios::app);
      if (f.is_open()) {
        f << line << "\n";
      }
      CWLOG_INFO(line.c_str());
    }
  }
  if (!leftover.empty()) {
    std::ofstream f(log_path, std::ios::binary | std::ios::app);
    if (f.is_open()) f << leftover << "\n";
  }
}

}  // namespace

const char* to_string(RunningMode mode) {
  switch (mode) {
    case RunningMode::Service: return "Service";
    case RunningMode::Sidecar: return "Sidecar";
    case RunningMode::NotRunning: return "NotRunning";
  }
  return "NotRunning";
}

std::wstring locate_core_path() {
  // ← state.rs:36-37 / service.rs:216-228：<home>\cores\win-mihomo.exe 优先（热升级内核）
  std::wstring custom = util::dirs::cores_dir() + L"\\win-mihomo.exe";
  if (fs::exists(custom)) return custom;
  // 兜底 exe 同级 sidecar（← current_exe().with_file_name("win-mihomo.exe")）
  std::wstring sidecar = util::dirs::sidecar_core_path();
  if (fs::exists(sidecar)) return sidecar;
  // 开发/测试布局兜底（copy_resources 落位 <exe>\sidecar\win-mihomo.exe）
  wchar_t self[MAX_PATH];
  if (::GetModuleFileNameW(nullptr, self, MAX_PATH) > 0) {
    std::wstring dev = (fs::path(self).parent_path() / L"sidecar\\win-mihomo.exe").wstring();
    if (fs::exists(dev)) return dev;
  }
  return {};
}

std::wstring build_sidecar_command_line(const SidecarLaunch& s) {
  // ← state.rs:56-79 参数顺序：-d <dir> -f <file> -ext-ctl-pipe <pipe>
  std::wstring cmd = L"\"" + s.core_path + L"\"";
  cmd += L" -d \"" + s.config_dir + L"\"";
  cmd += L" -f \"" + s.config_file + L"\"";
  cmd += L" -ext-ctl-pipe " + s.ipc_pipe;
  return cmd;
}

SidecarLaunch make_sidecar_launch(const std::wstring& core_path, const std::wstring& config_dir,
                                  const std::wstring& config_file, const std::wstring& ipc_pipe) {
  SidecarLaunch s;
  s.core_path = core_path;
  s.config_dir = config_dir;
  s.config_file = config_file;
  s.ipc_pipe = ipc_pipe;
  s.go_memlimit = env_or_default("GOMEMLIMIT", consts::kGoMemLimitDefault);
  s.go_gc = env_or_default("GOGC", consts::kGoGcDefault);
  s.go_maxprocs = env_or_default("GOMAXPROCS", consts::kGoMaxProcsDefault);
  return s;
}

std::vector<uint32_t> select_stale_core_pids(const std::vector<ProcInfo>& procs,
                                             const std::set<uint32_t>& exclude_pids,
                                             const std::vector<std::wstring>& scope_dirs) {
  std::vector<uint32_t> out;
  for (const auto& p : procs) {
    if (exclude_pids.count(p.pid)) continue;                          // 自身托管的不杀
    if (p.name.find(consts::kCoreProcessToken) == std::wstring::npos) continue;  // 同名才杀
    if (!p.path_ok || p.path.empty()) continue;                       // 路径无法查询 → 跳过
    bool in_scope = false;
    for (const auto& dir : scope_dirs) {
      // 大小写不敏感前缀匹配（Windows 路径语义），且要求目录边界完整
      auto norm = [](std::wstring v) {
        std::transform(v.begin(), v.end(), v.begin(), ::towlower);
        while (!v.empty() && v.back() == L'\\') v.pop_back();
        return v;
      };
      std::wstring nd = norm(dir);
      std::wstring np = norm(p.path);
      if (nd.empty()) continue;
      if (np.size() > nd.size() && np.compare(0, nd.size(), nd) == 0 && np[nd.size()] == L'\\') {
        in_scope = true;
        break;
      }
    }
    if (!in_scope) continue;  // 用户实机内核在作用域外 → 绝不触碰
    out.push_back(p.pid);
  }
  return out;
}

std::vector<ProcInfo> enumerate_all_processes() {
  std::vector<ProcInfo> out;
  HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return out;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  if (::Process32FirstW(snap, &pe)) {
    do {
      ProcInfo info;
      info.pid = pe.th32ProcessID;
      info.name = pe.szExeFile;
      if (info.name.find(consts::kCoreProcessToken) != std::wstring::npos) {
        // 只有同名进程才花代价查路径（select 规则要求 path_ok）
        HANDLE h = ::OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                 info.pid);
        if (h) {
          wchar_t path[MAX_PATH] = {};
          DWORD len = MAX_PATH;
          if (::QueryFullProcessImageNameW(h, 0, path, &len) && len > 0) {
            info.path = path;
            info.path_ok = true;
          }
          ::CloseHandle(h);
        }
        out.push_back(std::move(info));
      }
    } while (::Process32NextW(snap, &pe));
  }
  ::CloseHandle(snap);
  return out;
}

namespace {
bool terminate_pid(uint32_t pid) {
  HANDLE h = ::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
  if (!h) return false;
  BOOL ok = ::TerminateProcess(h, 1);
  if (ok) ::WaitForSingleObject(h, 3000);
  ::CloseHandle(h);
  return ok != FALSE;
}

// TerminateProcess 打不开时的兜底（对照任务卡 "toolhelp32 枚举 + taskkill"）
void taskkill_pid(uint32_t pid) {
  std::wstring cmd = L"taskkill.exe /F /T /PID " + std::to_wstring(pid);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                       nullptr, &si, &pi)) {
    ::WaitForSingleObject(pi.hProcess, 3000);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
  }
}
}  // namespace

std::vector<uint32_t> kill_all_mini_cores(const std::set<uint32_t>& exclude_pids) {
  std::vector<std::wstring> scope = {util::dirs::cores_dir(), util::dirs::exe_dir()};
  auto procs = enumerate_all_processes();
  auto targets = select_stale_core_pids(procs, exclude_pids, scope);
  std::vector<uint32_t> killed;
  for (uint32_t pid : targets) {
    if (terminate_pid(pid)) {
      killed.push_back(pid);
      CWLOG_WARN("killed leftover win-mihomo pid=" + std::to_string(pid));
    } else {
      taskkill_pid(pid);
      killed.push_back(pid);
      CWLOG_WARN("taskkill fallback for win-mihomo pid=" + std::to_string(pid));
    }
  }
  return killed;
}

// ---------- ICoreApiClient 适配 ----------
Result<BaseConfig> CoreApiClientAdapter::get_base_config() { return client_->get_base_config(); }

Result<ProxyItem> CoreApiClientAdapter::get_group_by_name(const std::string& name) {
  return client_->get_group_by_name(name);
}

Result<void> CoreApiClientAdapter::select_node_for_group(const std::string& group,
                                                         const std::string& node) {
  return client_->select_node_for_group(group, node);
}

Result<void> CoreApiClientAdapter::reload_config(bool force, const std::string& config_path) {
  return client_->reload_config(force, config_path);
}

// ---------- 恢复语义 ----------
bool is_builtin_outbound(const std::string& name) {
  return name == "DIRECT" || name == "REJECT" || name == "REJECT-DROP" || name == "PASS" ||
         name == "COMPATIBLE";
}

std::string extract_filter_text(const nlohmann::json& state, const std::string& uid) {
  if (!state.is_object() || !state.contains(uid)) return {};
  const auto& g = state[uid];
  if (!g.is_object() || !g.contains("PROXY")) return {};
  const auto& p = g["PROXY"];
  if (!p.is_object() || !p.contains("filterText")) return {};
  const auto& v = p["filterText"];
  if (!v.is_string()) return {};
  std::string s = v.get<std::string>();
  // trim + lowercase（← lifecycle.rs:437-440）
  auto is_space = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  size_t b = 0, e = s.size();
  while (b < e && is_space(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && is_space(static_cast<unsigned char>(s[e - 1]))) --e;
  s = s.substr(b, e - b);
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(::tolower(c));
  });
  return s;
}

std::string read_filter_text_lower() {
  try {
    auto state = clashwin::config::proxy_head_state::load();
    const auto& current = clashwin::config::Config::instance().profiles().latest_arc()->current;
    if (!current.has_value()) return {};
    return extract_filter_text(state, *current);
  } catch (...) {
    return {};
  }
}

std::string pick_fallback_node(const std::vector<std::string>& all, const std::string& filter_lower) {
  auto non_dummy = [](const std::string& n) { return !clashwin::util::is_dummy_node(n); };
  for (const auto& n : all) {
    if (!non_dummy(n)) continue;
    if (filter_lower.empty() ||
        n.size() >= filter_lower.size()) {  // contains 语义（ASCII lower 对比）
      std::string low = n;
      std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) {
        return static_cast<char>(::tolower(c));
      });
      if (filter_lower.empty() || low.find(filter_lower) != std::string::npos) return n;
    }
  }
  for (const auto& n : all) {
    if (non_dummy(n)) return n;
  }
  return {};
}

std::string select_fallback_in_subset(ICoreApiClient& api, const std::string& filter_lower) {
  auto group = api.get_group_by_name("PROXY");
  if (group.is_err() || !group.value().all.has_value()) return {};
  auto fb = pick_fallback_node(*group.value().all, filter_lower);
  if (fb.empty()) return {};
  auto sel = api.select_node_for_group("PROXY", fb);
  if (sel.is_err()) {
    CWLOG_WARN("PROXY fallback select failed: " + sel.error().message);
    return {};
  }
  CWLOG_INFO("PROXY fallback node selected: " + fb);
  return fb;
}

// ---------- CoreManager ----------
CoreManager::CoreManager() = default;

CoreManager::~CoreManager() {
  // 析构兜底：不主动杀进程（生产由退出序列负责），仅关闭句柄
  log_run_.store(false);
  for (auto* t : log_threads_) {
    if (t) ::WaitForSingleObject(t, 1000);
  }
  if (child_) ::CloseHandle(child_);
}

CoreManager& CoreManager::instance() {
  static CoreManager m;
  return m;
}

RunningMode CoreManager::running_mode() const { return mode_.load(); }

void CoreManager::notify(const char* status, const std::string& message) {
  if (notifier_) {
    notifier_(status, message);
  } else {
    CWLOG_WARN(std::string("[notice:") + status + "] " + message);
  }
}

bool CoreManager::sidecar_alive() const {
  if (!child_) return false;
  return ::WaitForSingleObject(child_, 0) == WAIT_TIMEOUT;
}

bool CoreManager::core_alive() const {
  if (mode_.load() != RunningMode::Service) return sidecar_alive();
  // 服务模式：内核是 SCM 服务的子进程，本进程没有 child_ 句柄，
  // 只能问服务 /status 的 core_pid（null = 未运行）。
  mihomo::ServiceIpc ipc;
  if (!ipc.connect().is_ok()) return false;
  auto st = ipc.get_status();
  if (!st.is_ok() || !st.value().is_object()) return false;
  const auto& d = st.value();
  if (!d.contains("core_pid")) return false;
  if (d["core_pid"].is_number()) return d["core_pid"].get<long long>() != 0;
  return false;
}

Result<void> CoreManager::start_sidecar(const std::wstring& config_file,
                                        const std::wstring& ipc_pipe) {
  std::lock_guard<std::mutex> lock(lifecycle_);
  return start_sidecar_locked(config_file, ipc_pipe);
}

Result<void> CoreManager::start_sidecar_locked(const std::wstring& config_file,
                                               const std::wstring& ipc_pipe) {
  // ← lifecycle.rs:39-47 幂等：已有内核运行时 no-op
  if (mode_.load() != RunningMode::NotRunning) {
    CWLOG_INFO("start_core called while a core is running; treated as no-op");
    return Result<void>::ok();
  }
  if (config_file.empty()) {
    return Result<void>::err({2301, "config file path is empty"});
  }

  // ← state.rs:29 启动前强杀残留（排除自身 pid；此时无子进程）
  std::set<uint32_t> exclude = {::GetCurrentProcessId()};
  kill_all_mini_cores(exclude);

  auto core = locate_core_path();
  if (core.empty()) {
    return Result<void>::err({2302, "win-mihomo core not found (cores/ and sidecar both missing)"});
  }

  auto home = util::dirs::app_home_dir();
  std::wstring pipe = ipc_pipe.empty() ? util::dirs::ipc_path() : ipc_pipe;
  auto launch = make_sidecar_launch(core, home, config_file, pipe);
  std::wstring cmd = build_sidecar_command_line(launch);
  CWLOG_INFO("Starting core in sidecar mode: " + utf::utf16_to_utf8(cmd));

  // stdout/stderr 管道（← state.rs:89-119 消费语义）
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE out_r = nullptr, out_w = nullptr, err_r = nullptr, err_w = nullptr;
  if (!::CreatePipe(&out_r, &out_w, &sa, 0) || !::CreatePipe(&err_r, &err_w, &sa, 0)) {
    return Result<void>::err({2303, "CreatePipe failed, gle=" + std::to_string(::GetLastError())});
  }
  // 父进程端读句柄不可继承
  ::SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  ::SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_SHOWMINNOACTIVE;
  si.hStdOutput = out_w;
  si.hStdError = err_w;
  PROCESS_INFORMATION pi{};
  std::wstring env_block = build_sidecar_env_block(launch);

  if (!::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                        env_block.empty() ? nullptr : env_block.data(), nullptr, &si, &pi)) {
    ::CloseHandle(out_r);
    ::CloseHandle(out_w);
    ::CloseHandle(err_r);
    ::CloseHandle(err_w);
    return Result<void>::err({2304, "CreateProcessW(win-mihomo) failed, gle=" +
                                        std::to_string(::GetLastError())});
  }
  ::CloseHandle(pi.hThread);
  // 父进程关闭写端，使读端在子进程退出后收到 EOF
  ::CloseHandle(out_w);
  ::CloseHandle(err_w);

  child_ = pi.hProcess;
  pid_ = pi.dwProcessId;
  stdout_read_ = out_r;
  stderr_read_ = err_r;
  mode_.store(RunningMode::Sidecar);
  CWLOG_INFO("Sidecar started with PID: " + std::to_string(pid_));

  // 日志消费线程（写 logs/sidecar/sidecar_latest.log）
  auto log_dir = util::dirs::sidecar_log_dir();
  std::wstring log_path = log_dir + L"\\sidecar_latest.log";
  log_run_.store(true);
  log_threads_.clear();
  auto spawn_thread = [&](HANDLE h) {
    auto* th = new std::thread([h, log_path, this]() {
      log_consumer_thread(h, log_path, &log_run_);
      ::CloseHandle(h);
    });
    log_threads_.push_back(reinterpret_cast<void*>(th->native_handle()));
    th->detach();  // 句柄已留档等待，线程对象释放
  };
  spawn_thread(out_r);
  spawn_thread(err_r);
  return Result<void>::ok();
}

Result<void> CoreManager::start_auto(const std::wstring& config_file,
                                     const std::wstring& ipc_pipe) {
  std::lock_guard<std::mutex> lock(lifecycle_);
  if (mode_.load() != RunningMode::NotRunning) {
    CWLOG_INFO("start_auto called while a core is running; treated as no-op");
    return Result<void>::ok();
  }
  bool needs_tun = false;
  if (auto v = config::Config::instance().verge().latest_arc()) {
    needs_tun = v->enable_tun_mode.value_or(false);
  }
  const bool is_admin_user = config::Config::instance().is_admin();
  auto runner = system::get_service_runner();
  const bool service_ready = runner->is_available();
  auto decision = service_mode::decide_startup_mode(needs_tun, is_admin_user,
                                                    service_ready);
  CWLOG_INFO(std::string("core start decision: needs_tun=") + (needs_tun ? "1" : "0") +
             " is_admin=" + (is_admin_user ? "1" : "0") +
             " service_ready=" + (service_ready ? "1" : "0") + " → " +
             (decision.use_service ? "Service" : "Sidecar"));

  if (!decision.use_service) {
    // TUN 请求 + 非管理员：即使 service 当下不可用，也先走安装/启动流程；
    // 仍失败则明确告警（BUG-298 铁律：禁止静默降级）
    if (needs_tun && !is_admin_user) {
      auto r = start_service_locked(config_file);
      if (r.is_ok()) return r;
      CWLOG_WARN("TUN requested but service unavailable: " + r.error().message);
      notify("service_unavailable",
             "clash_verge_service 不可用，TUN 无法启用: " + r.error().message);
      return Result<void>::err({2302, "TUN 模式需要特权服务 clash_verge_service，服务启动失败: " + r.error().message});
    }
    return start_sidecar_locked(config_file, ipc_pipe);
  }
  return start_service_locked(config_file);
}

Result<void> CoreManager::start_service_locked(const std::wstring& config_file) {
  if (config_file.empty()) {
    return Result<void>::err({2301, "config file path is empty"});
  }
  // ← Mini: src-tauri/src/core/manager/lifecycle.rs:151-176 prepare_startup_mode()
  //          + :183-235 wait_for_service_ready()（含 installer UAC）
  //   链路：未安装 / ImagePath 失效 → UAC 安装 → 复核注册 → 启动 → IPC 就绪 → 起内核。
  // 先清用户态残留（服务侧 start_clash 内部也会 stop_clash）
  std::set<uint32_t> exclude = {::GetCurrentProcessId()};
  kill_all_mini_cores(exclude);

  auto runner = system::get_service_runner();
  // 服务缺失 / 已注册但 ImagePath 失效（Mini 卸载后的残壳）→ 安装流程（唯一名 clash_verge_service，需 UAC）
  const bool installed = runner->is_installed();
  const bool binary_ok = installed && service_mode::is_service_binary_valid();
  if (!installed || !binary_ok) {
    CWLOG_WARN(installed ? "clash_verge_service ImagePath 失效 → 启动安装流程（唯一服务名）"
                         : "clash_verge_service 未安装 → 启动安装流程（唯一服务名）");
    notify("service_missing", installed
                                  ? "clash_verge_service 的 ImagePath 已失效（Clash Mini 可能已卸载），正在安装共用服务（需要 UAC 确认）"
                                  : "clash_verge_service 未安装，正在启动安装程序（需要 UAC 确认）");
    auto ir = runner->run_installer(true);
    if (ir.is_err()) {
      notify("service_install_error", "安装共用服务 clash_verge_service 失败（UAC 取消或安装器错误）：" +
                                          ir.error().message);
      return ir;
    }
    if (!runner->is_installed()) {
      notify("service_install_error", "安装器已执行但 clash_verge_service 仍未注册，请重试或先通过 Clash Mini 安装");
      return Result<void>::err({2316, "installer ran but clash_verge_service is still not registered"});
    }
  }
  if (!runner->is_available()) {
    auto sr = runner->start_service();
    if (sr.is_err()) {
      notify("service_start_error", "clash_verge_service 启动失败（可能需要 UAC）：" + sr.error().message);
      return sr;
    }
    constexpr int kServiceReadyWaitMs = 5000;  // ← service.rs wait_for_ready 上限
    if (!runner->wait_for_ready(kServiceReadyWaitMs)) {
      notify("service_start_error",
             "clash_verge_service 已启动但 IPC 未就绪（5s 超时）；请确认服务以 SYSTEM 运行后重试");
      return Result<void>::err(
          {2317, "clash_verge_service not ready after start (IPC probe timeout)"});
    }
  }

  mihomo::ServiceIpc ipc;
  auto cr = ipc.connect();
  if (cr.is_err()) {
    return Result<void>::err({2318, "connect service pipe failed: " + cr.error().message});
  }
  service_mode::ServiceIpcAdapter adapter(&ipc);

  auto core = locate_core_path();
  if (core.empty()) {
    return Result<void>::err({2302, "win-mihomo core not found (cores/ and sidecar both missing)"});
  }
  auto home = util::dirs::app_home_dir();
  auto payload = service_mode::build_clash_config_payload(
      utf::utf16_to_utf8(core), utf::utf16_to_utf8(util::dirs::ipc_path()),
      utf::utf16_to_utf8(config_file), utf::utf16_to_utf8(home),
      service_mode::default_log_config());

  // mixed-port（verge 优先，clash 兜底）用于 stop 后端口释放轮询
  uint16_t port = 0;
  if (auto v = config::Config::instance().verge().latest_arc()) {
    port = v->verge_mixed_port.value_or(0);
  }
  if (port == 0) {
    auto& clash = config::Config::instance().clash();
    auto arc = clash.latest_arc();
    if (arc) {
      try {
        const auto& m = arc->mapping();
        if (m.IsMap() && m["mixed-port"]) {
          port = static_cast<uint16_t>(m["mixed-port"].as<int>());
        }
      } catch (...) {
        port = 0;
      }
    }
  }

  auto sr = service_mode::start_with_service(adapter, payload, port);
  if (sr.is_err()) return sr;
  mode_.store(RunningMode::Service);
  pid_ = 0;  // 内核归 SCM 服务持有，非本进程子进程
  CWLOG_INFO("Service started core successfully (elevated SYSTEM, TUN-capable)");
  return Result<void>::ok();
}

Result<void> CoreManager::stop_sidecar_locked() {
  // ← state.rs:126-154
  if (child_) {
    HANDLE h = child_;
    child_ = nullptr;
    ::TerminateProcess(h, 0);
    ::WaitForSingleObject(h, consts::kSidecarStopWaitMs);
    ::CloseHandle(h);
    pid_ = 0;
  }
  // 日志线程随 EOF 自行退出
  log_run_.store(false);
  log_threads_.clear();
  stdout_read_ = nullptr;
  stderr_read_ = nullptr;
  mode_.store(RunningMode::NotRunning);
  // ← state.rs:153 kill_all 兜底（排除自身）
  std::set<uint32_t> exclude = {::GetCurrentProcessId()};
  kill_all_mini_cores(exclude);
  return Result<void>::ok();
}

Result<void> CoreManager::stop() {
  std::lock_guard<std::mutex> lock(lifecycle_);
  RunningMode m = mode_.load();
  if (m == RunningMode::Sidecar) {
    return stop_sidecar_locked();
  }
  if (m == RunningMode::Service) {
    // ← service.rs::stop_core_by_service：DELETE /clash/stop；失败再 kill_all 兜底
    mihomo::ServiceIpc ipc;
    if (ipc.connect().is_ok()) {
      service_mode::ServiceIpcAdapter adapter(&ipc);
      auto sr = service_mode::stop_with_service(adapter);
      if (sr.is_err()) {
        CWLOG_WARN("stop via service failed (fallback kill_all): " + sr.error().message);
      }
    } else {
      CWLOG_WARN("stop service core: connect service pipe failed (fallback kill_all)");
    }
    std::set<uint32_t> exclude = {::GetCurrentProcessId()};
    kill_all_mini_cores(exclude);
    mode_.store(RunningMode::NotRunning);
    return Result<void>::ok();
  }
  return Result<void>::ok();
}

Result<void> CoreManager::restart_with_restore(ICoreApiClient& api, const CoreOps& ops,
                                               const RestoreTimings& t) {
  // ← lifecycle.rs:108-136：快照 → 停 → 启 → 恢复
  auto saved = snapshot_proxy_group_now(api);
  if (!saved.has_value()) {
    // 进程可能已被杀死，从持久化配置中读取选定节点作为保底
    auto profiles = config::Config::instance().profiles().latest_arc();
    if (profiles && profiles->current) {
      if (const auto* item = profiles->get_item(*profiles->current)) {
        if (item->selected) {
          for (const auto& s : *item->selected) {
            if (s.name.value_or("") == "PROXY" && s.now && !s.now->empty()) {
              saved = *s.now;
              break;
            }
          }
        }
      }
    }
  }
  auto st = ops.stop();
  if (st.is_err()) return st;
  st = ops.start();
  if (st.is_err()) return st;
  if (saved.has_value()) {
    restore_proxy_group_now(api, *saved, t);
  } else {
    // 进程冷启动 / 无历史 snapshot 时，自动在 filterText 子集内兜底选点，
    // 避免 Selector 落回首个广告/假节点（← lifecycle.rs:354-416 兜底防线）
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(t.api_ready_budget_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      if (api.get_base_config().is_ok()) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(t.api_ready_poll_ms));
    }
    select_fallback_in_subset(api, read_filter_text_lower());
  }
  return Result<void>::ok();
}

std::optional<std::string> CoreManager::snapshot_proxy_group_now(ICoreApiClient& api) {
  // ← lifecycle.rs:284-302：读取失败/now 空 → 无需恢复
  auto group = api.get_group_by_name("PROXY");
  if (group.is_err()) {
    CWLOG_WARN("snapshot PROXY group failed (ignored, no restore): " + group.error().message);
    return std::nullopt;
  }
  const auto& now = group.value().now;
  if (now.has_value() && !now->empty()) {
    CWLOG_INFO("snapshot PROXY group now: " + *now);
    return *now;
  }
  return std::nullopt;
}

void CoreManager::restore_proxy_group_now(ICoreApiClient& api, const std::string& node,
                                          const RestoreTimings& t) {
  // ← lifecycle.rs:307-416
  // 1) 等 API 就绪（get_base_config ok，200ms 轮询，预算 30s）
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(t.api_ready_budget_ms);
  bool api_ready = false;
  while (std::chrono::steady_clock::now() < deadline) {
    if (api.get_base_config().is_ok()) {
      api_ready = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(t.api_ready_poll_ms));
  }
  if (!api_ready) {
    CWLOG_WARN("core API not ready in budget, skip PROXY restore");
    return;
  }

  // 2) 等 PROXY 组成员填充（500ms 轮询，预算 20s），顺带判定目标节点是否在组内
  deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(t.group_fill_budget_ms);
  bool node_in_group = false;
  while (std::chrono::steady_clock::now() < deadline) {
    auto g = api.get_group_by_name("PROXY");
    if (g.is_ok() && g.value().all.has_value() && !g.value().all->empty()) {
      node_in_group = std::find(g.value().all->begin(), g.value().all->end(), node) !=
                      g.value().all->end();
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(t.group_fill_poll_ms));
  }

  // 3) 内置出站 / 不在组内 → 跳过注定失败的 select，直接子集兜底
  if (is_builtin_outbound(node)) {
    CWLOG_INFO("skip PROXY restore: " + node + " is builtin outbound, fallback to subset");
    select_fallback_in_subset(api, read_filter_text_lower());
    return;
  }
  if (!node_in_group) {
    CWLOG_INFO("skip PROXY restore: node " + node + " not in group, fallback to subset");
    select_fallback_in_subset(api, read_filter_text_lower());
    return;
  }

  // 4) 90s 重试预算内 select（BUG-301：冷启动 IPC 可能连续超时，失败重试 3s 间隔）
  deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(t.select_budget_ms);
  for (;;) {
    auto sel = api.select_node_for_group("PROXY", node);
    if (sel.is_ok()) {
      CWLOG_INFO("PROXY node restored: " + node);
      return;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      CWLOG_WARN("PROXY restore budget exhausted (" + node + "), fallback to subset");
      select_fallback_in_subset(api, read_filter_text_lower());
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(t.select_retry_ms));
  }
}

Result<void> CoreManager::apply_config(ICoreApiClient& api, const std::string& config_path,
                                       const CoreOps& ops, const RestoreTimings& t) {
  // ← manager/config.rs:121-167：snapshot → reload(force) → restore；失败整核重启兜底
  auto saved = snapshot_proxy_group_now(api);
  auto reload = api.reload_config(true, config_path);
  if (reload.is_ok()) {
    CWLOG_INFO("Configuration applied");
    if (saved.has_value()) {
    restore_proxy_group_now(api, *saved, t);
  } else {
    // 进程冷启动 / 无历史 snapshot 时，自动在 filterText 子集内兜底选点，
    // 避免 Selector 落回首个广告/假节点（← lifecycle.rs:354-416 兜底防线）
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(t.api_ready_budget_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      if (api.get_base_config().is_ok()) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(t.api_ready_poll_ms));
    }
    select_fallback_in_subset(api, read_filter_text_lower());
  }
    return Result<void>::ok();
  }
  CWLOG_WARN("Failed to apply config via mihomo api (" + reload.error().message +
             "), restart core to apply it");
  auto rs = restart_with_restore(api, ops, t);
  if (rs.is_err()) {
    return Result<void>::err({2305, "Failed to apply config: " + rs.error().message});
  }
  CWLOG_INFO("Configuration applied after restart");
  return Result<void>::ok();
}

}  // namespace clashwin::core

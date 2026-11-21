#include "clashwin/update/client_updater.h"

#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>

#pragma comment(lib, "bcrypt.lib")

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "config/config.h"
#include "config/update_keys.h"
#include "events/event_bus.h"
#include "system/net_http.h"
#include "util/base64.h"
#include "util/blake2b.h"
#include "util/dirs.h"
#include "util/ed25519.h"
#include "util/log.h"
#include "util/str_utils.h"
#include "util/utf.h"

namespace clashwin::update {

namespace {

using clashwin::utf::utf16_to_utf8;
using clashwin::utf::utf8_to_utf16;
using util::starts_with;
using util::to_lower;
using util::trim;

inline Error make_err(const std::string& msg, int code = 1) {
  return Error{code, msg};
}

// src: use-client-update.ts:63-105 客户端更新进度事件（status: downloading/done/error）
void emit_client_progress(const std::string& status, int progress,
                          const std::string& message) {
  events::EventBus::instance().publish(
      "client-update-progress",
      nlohmann::json{{"status", status}, {"progress", progress}, {"message", message}});
}

// src: locales/zh/layout.json:73-74 「已下载 {{percent}}% ({{downloaded}} MB / {{total}} MB)」
std::string format_mb(double bytes) {
  std::ostringstream oss;
  oss.setf(std::ios::fixed);
  oss.precision(2);
  oss << bytes / (1024.0 * 1024.0);
  return oss.str();
}

// ---------- minisign 验签（B-D44 收敛） ----------
// 1:1 对齐 tauri-plugin-updater 2.10.1 verify_signature（updater.rs:1452-1463，
// allow_legacy=true）+ jedisct1/rust-minisign-verify 语义：
//   公钥 = Base64(pubkey_file_b64) → 两行文本 → 第 2 行 Base64 → 42 字节
//     （"Ed"(2) + keynum(8) + Ed25519 公钥(32)）；
//   签名 = Base64(manifest signature) → 四行文本 → bin1(74: 算法(2)+keynum(8)+
//     sig(64)) + "trusted comment: " 行 + global_sig(64)；
//   校验顺序：keynum 匹配 → 主签名（"ED"=Ed25519(BLAKE2b-512(file))；
//     "Ed"=Ed25519(file)）→ 全局签名 Ed25519(sig(64) ++ trusted_comment[17..])。
bool verify_with_pubkey(const std::vector<uint8_t>& file_bytes,
                        const std::string& signature_b64,
                        const char* pubkey_file_b64, std::string* err) {
  auto fail = [&](const char* msg) {
    if (err) *err = msg;
    return false;
  };

  // ---- 公钥解析 ----
  const auto pk_file = util::decode_base64_robust(pubkey_file_b64);
  if (!pk_file) return fail("更新公钥未配置或格式错误，已中止安装");
  const std::string pk_text(pk_file->begin(), pk_file->end());
  const size_t nl1 = pk_text.find('\n');
  if (nl1 == std::string::npos) return fail("更新公钥格式错误，已中止安装");
  size_t nl2 = pk_text.find('\n', nl1 + 1);
  std::string pk_b64 = pk_text.substr(
      nl1 + 1, nl2 == std::string::npos ? std::string::npos : nl2 - nl1 - 1);
  if (!pk_b64.empty() && pk_b64.back() == '\r') pk_b64.pop_back();
  const auto pk_bin = util::decode_base64_robust(pk_b64);
  if (!pk_bin || pk_bin->size() != 42) return fail("更新公钥格式错误，已中止安装");
  const uint8_t algo_pk[2] = {(*pk_bin)[0], (*pk_bin)[1]};
  if (!((algo_pk[0] == 0x45 && algo_pk[1] == 0x64) ||
        (algo_pk[0] == 0x45 && algo_pk[1] == 0x44)))
    return fail("更新公钥算法不受支持，已中止安装");
  const uint8_t* key_id = pk_bin->data() + 2;
  const uint8_t* pk32 = pk_bin->data() + 10;

  // ---- 签名解析 ----
  const auto sig_text_bytes = util::decode_base64_robust(signature_b64);
  if (!sig_text_bytes) return fail("发布清单缺少有效签名，已中止安装");
  const std::string sig_text(sig_text_bytes->begin(), sig_text_bytes->end());
  std::vector<std::string> lines;
  {
    std::istringstream iss(sig_text);
    std::string ln;
    while (std::getline(iss, ln)) {
      if (!ln.empty() && ln.back() == '\r') ln.pop_back();
      lines.push_back(ln);
    }
  }
  // minisign .sig：注释 / bin1 / trusted comment / global sig（各行非空）
  if (lines.size() < 4) return fail("签名格式错误，已中止安装");
  const auto bin1 = util::decode_base64_robust(lines[1]);
  if (!bin1 || bin1->size() != 74) return fail("签名格式错误，已中止安装");
  const bool prehashed = (*bin1)[0] == 0x45 && (*bin1)[1] == 0x44;  // "ED"
  const bool legacy_raw = (*bin1)[0] == 0x45 && (*bin1)[1] == 0x64;  // "Ed"
  if (!prehashed && !legacy_raw) return fail("签名算法不受支持，已中止安装");
  if (std::memcmp(bin1->data() + 2, key_id, 8) != 0)
    return fail("签名与更新公钥不匹配，已中止安装");
  const uint8_t* sig64 = bin1->data() + 10;
  if (lines[2].rfind("trusted comment: ", 0) != 0)
    return fail("签名格式错误，已中止安装");
  const std::string trusted_comment = lines[2].substr(17);  // [17..]
  const auto global_sig = util::decode_base64_robust(lines[3]);
  if (!global_sig || global_sig->size() != 64)
    return fail("签名格式错误，已中止安装");

  // ---- 数据：预哈希变体先 BLAKE2b-512 ----
  std::vector<uint8_t> data;
  if (prehashed) {
    data = util::Blake2b::digest(file_bytes.data(), file_bytes.size(), 64);
  } else {
    data = file_bytes;
  }

  // ---- 主签名 + 全局签名（minisign-verify verify_ed25519 顺序）----
  if (!util::ed25519_verify(data.data(), data.size(), sig64, pk32))
    return fail("安装包签名校验失败");
  std::vector<uint8_t> gmsg(sig64, sig64 + 64);
  gmsg.insert(gmsg.end(), trusted_comment.begin(), trusted_comment.end());
  if (!util::ed25519_verify(gmsg.data(), gmsg.size(), global_sig->data(), pk32))
    return fail("安装包签名校验失败");
  return true;
}

// BUG-417：从 Verge / Clash 运行时配置解析本地 mixed-port（verge 优先，clash 兜底，默认 10801）
uint16_t resolve_configured_mixed_port() {
  std::optional<int> verge_port;
  std::optional<int> clash_port;
  try {
    auto v = config::Config::instance().verge().latest_arc();
    if (v && v->verge_mixed_port.has_value()) {
      verge_port = static_cast<int>(*v->verge_mixed_port);
    }
  } catch (...) {
  }
  try {
    auto c = config::Config::instance().clash().latest_arc();
    if (c && c->mapping().IsMap() && c->mapping()["mixed-port"]) {
      clash_port = c->mapping()["mixed-port"].as<int>();
    }
  } catch (...) {
  }
  return ClientUpdater::resolve_local_mixed_port(verge_port, clash_port);
}

class DefaultAppUpdateDownloader : public IAppUpdateDownloader {
 public:
  explicit DefaultAppUpdateDownloader(std::function<void(uint32_t)> sleep_fn)
      : sleep_fn_(std::move(sleep_fn)) {}

  Result<std::string> fetch_manifest(const std::string& endpoint_url) override {
    const uint16_t local_port = resolve_configured_mixed_port();
    const std::string proxy_url =
        "http://127.0.0.1:" + std::to_string(local_port);
    auto mixed_provider = [local_port]() -> std::optional<uint16_t> {
      return local_port;
    };

    // 1. 优先走本地代理（15s 超时，← client-update.ts checkClientUpdateWithFallback）
    {
      system::HttpOptions opts;
      opts.proxy_type = system::ProxyType::Localhost;
      opts.timeout_seconds = ClientUpdater::kCheckProxyTimeoutSecs;
      opts.user_agent = "ClashWin-ClientUpdater";
      opts.localhost_mixed_port = mixed_provider;
      auto res = system::http_get(endpoint_url, opts);
      if (res.is_ok() && res.value().is_success()) {
        return Result<std::string>::ok(res.value().body);
      }
      const std::string err_detail =
          res.is_ok() ? ("HTTP " + std::to_string(res.value().status))
                      : res.error().message;
      CWLOG_ERROR("[ClientUpdate] 经本地代理 (" + proxy_url +
                  ") 检查更新失败，回退系统/直连通道重试: " + err_detail);
      if (sleep_fn_) {
        sleep_fn_(ClientUpdater::kCheckRetryDelayMs);
      } else {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(ClientUpdater::kCheckRetryDelayMs));
      }
    }

    // 2. 回退系统代理与直连（20s 超时）
    std::string last_err = "unknown error";
    for (auto pt : {system::ProxyType::System, system::ProxyType::None}) {
      system::HttpOptions opts;
      opts.proxy_type = pt;
      opts.timeout_seconds = ClientUpdater::kCheckDirectTimeoutSecs;
      opts.user_agent = "ClashWin-ClientUpdater";
      auto res = system::http_get(endpoint_url, opts);
      if (res.is_ok() && res.value().is_success()) {
        return Result<std::string>::ok(res.value().body);
      }
      last_err = res.is_ok() ? ("HTTP " + std::to_string(res.value().status))
                             : res.error().message;
    }
    CWLOG_ERROR("[ClientUpdate] 检查客户端更新最终失败: " + last_err);
    return Result<std::string>::err(make_err(
        "failed to fetch client update manifest across all proxy modes: " +
        last_err));
  }

  Result<void> download_installer(
      const std::string& url, const std::wstring& dest_path,
      std::function<void(size_t downloaded, size_t total)> on_progress) override {
    const uint16_t local_port = resolve_configured_mixed_port();
    auto mixed_provider = [local_port]() -> std::optional<uint16_t> {
      return local_port;
    };
    const std::vector<system::ProxyType> proxy_types = {
        system::ProxyType::Localhost, system::ProxyType::System,
        system::ProxyType::None};

    std::string last_err = "unknown error";
    for (auto pt : proxy_types) {
      system::HttpOptions opts;
      opts.proxy_type = pt;
      opts.timeout_seconds = ClientUpdater::kDownloadAttemptTimeoutSecs;
      opts.user_agent = "ClashWin-ClientUpdater";
      opts.localhost_mixed_port = mixed_provider;
      auto res = system::http_download_file(
          url, dest_path, opts,
          [&on_progress](uint64_t downloaded, uint64_t total) {
            if (on_progress) {
              on_progress(static_cast<size_t>(downloaded),
                          static_cast<size_t>(total));
            }
          });
      if (res.is_ok()) {
        return Result<void>::ok();
      }
      last_err = res.error().message;
      std::error_code ec;
      std::filesystem::remove(dest_path, ec);
    }
    return Result<void>::err(make_err(
        "failed to download client installer across all proxy modes: " +
        last_err));
  }

 private:
  std::function<void(uint32_t)> sleep_fn_;
};

std::vector<int> parse_version_numbers(const std::string& ver_str) {
  std::string s = trim(ver_str);
  if (!s.empty() && (s.front() == 'v' || s.front() == 'V')) {
    s = s.substr(1);
  }
  // Trim pre-release tags like -beta, -rc, +build
  size_t tag_pos = s.find_first_of("-+");
  if (tag_pos != std::string::npos) {
    s = s.substr(0, tag_pos);
  }

  std::vector<int> nums;
  std::istringstream iss(s);
  std::string part;
  while (std::getline(iss, part, '.')) {
    try {
      nums.push_back(std::stoi(part));
    } catch (...) {
      nums.push_back(0);
    }
  }
  return nums;
}

}  // namespace

// 测试注入钩子：nullptr = 恢复 config 公钥（Mini tauri.conf.json:32）
namespace {
const char* g_pubkey_override = nullptr;
}  // namespace

void set_verify_pubkey_for_test(const char* pubkey_file_b64) {
  g_pubkey_override = pubkey_file_b64;
}

bool verify_installer_signature(const std::vector<uint8_t>& file_bytes,
                                const std::string& signature_b64,
                                std::string* err,
                                const char* pubkey_file_b64_override) {
  const char* pk = pubkey_file_b64_override
                       ? pubkey_file_b64_override
                       : (g_pubkey_override ? g_pubkey_override
                                            : config::kUpdatePubkeyFileB64);
  return verify_with_pubkey(file_bytes, signature_b64, pk, err);
}

ClientUpdater::ClientUpdater()
    : downloader_(std::make_shared<DefaultAppUpdateDownloader>(
          [this](uint32_t ms) { sleep_for_retry(ms); })) {}

std::string ClientUpdater::compute_sha256_hex(const std::wstring& file_path) {
  std::ifstream in(file_path, std::ios::binary);
  if (!in.is_open()) return {};

  BCRYPT_ALG_HANDLE alg = nullptr;
  if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM,
                                                  nullptr, 0))) {
    return {};
  }
  std::string out;
  DWORD obj_len = 0, hash_len = 0, cb = 0;
  if (BCRYPT_SUCCESS(BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
                                       reinterpret_cast<PUCHAR>(&obj_len),
                                       sizeof(obj_len), &cb, 0)) &&
      BCRYPT_SUCCESS(BCryptGetProperty(alg, BCRYPT_HASH_LENGTH,
                                       reinterpret_cast<PUCHAR>(&hash_len),
                                       sizeof(hash_len), &cb, 0))) {
    std::vector<uint8_t> obj(obj_len);
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, obj.data(), obj_len,
                                        nullptr, 0, 0))) {
      std::vector<char> buf(65536);
      bool read_ok = true;
      while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = in.gcount();
        if (got > 0 &&
            !BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(buf.data()),
                                           static_cast<ULONG>(got), 0))) {
          read_ok = false;
          break;
        }
      }
      std::vector<uint8_t> digest(hash_len);
      if (read_ok && in.eof() &&
          BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), hash_len, 0))) {
        static const char* kHex = "0123456789abcdef";
        out.reserve(digest.size() * 2);
        for (auto b : digest) {
          out.push_back(kHex[b >> 4]);
          out.push_back(kHex[b & 0x0F]);
        }
      }
      BCryptDestroyHash(hash);
    }
  }
  BCryptCloseAlgorithmProvider(alg, 0);
  return out;
}

ClientUpdater& ClientUpdater::instance() {
  static ClientUpdater inst;
  return inst;
}

void ClientUpdater::set_downloader(std::shared_ptr<IAppUpdateDownloader> downloader) {
  downloader_ = std::move(downloader);
}

void ClientUpdater::set_sleep_fn(std::function<void(uint32_t ms)> fn) {
  sleep_fn_ = std::move(fn);
}

void ClientUpdater::sleep_for_retry(uint32_t ms) const {
  if (ms == 0) return;
  if (sleep_fn_) {
    sleep_fn_(ms);
  } else {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  }
}

uint16_t ClientUpdater::resolve_local_mixed_port(std::optional<int> verge_mixed_port,
                                                 std::optional<int> clash_mixed_port) {
  // ← client-update.ts resolveLocalMixedProxyUrl（BUG-417）
  auto valid_port = [](std::optional<int> p) -> std::optional<uint16_t> {
    if (p.has_value() && *p >= 1 && *p <= 65535) {
      return static_cast<uint16_t>(*p);
    }
    return std::nullopt;
  };
  if (auto p = valid_port(verge_mixed_port)) return *p;
  if (auto p = valid_port(clash_mixed_port)) return *p;
  return kDefaultMixedPort;
}

std::string ClientUpdater::resolve_local_mixed_proxy_url(
    std::optional<int> verge_mixed_port, std::optional<int> clash_mixed_port) {
  return "http://127.0.0.1:" +
         std::to_string(resolve_local_mixed_port(verge_mixed_port, clash_mixed_port));
}

uint32_t ClientUpdater::download_retry_delay_ms(int attempt) {
  if (attempt == 1) return 1500;
  if (attempt == 2) return 2500;
  return 0;
}

void ClientUpdater::set_current_version(const std::string& version) {
  current_version_ = version;
}

bool ClientUpdater::is_version_newer(const std::string& latest, const std::string& current) {
  auto l_nums = parse_version_numbers(latest);
  auto c_nums = parse_version_numbers(current);

  size_t max_len = std::max(l_nums.size(), c_nums.size());
  for (size_t i = 0; i < max_len; ++i) {
    int l_val = (i < l_nums.size()) ? l_nums[i] : 0;
    int c_val = (i < c_nums.size()) ? c_nums[i] : 0;
    if (l_val > c_val) return true;
    if (l_val < c_val) return false;
  }
  return false;
}

Result<AppUpdateInfo> ClientUpdater::parse_manifest(
    const std::string& json_text, const std::string& current_version) {
  try {
    auto j = nlohmann::json::parse(json_text);
    AppUpdateInfo info;
    info.current_version = current_version;

    if (j.contains("version") && j["version"].is_string()) {
      info.latest_version = j["version"].get<std::string>();
    }
    if (j.contains("notes") && j["notes"].is_string()) {
      info.notes = j["notes"].get<std::string>();
    }
    if (j.contains("pub_date") && j["pub_date"].is_string()) {
      info.pub_date = j["pub_date"].get<std::string>();
    }

    // Platforms lookup (Tauri updater schema)
    if (j.contains("platforms") && j["platforms"].is_object()) {
      const auto& platforms = j["platforms"];
      for (auto it = platforms.begin(); it != platforms.end(); ++it) {
        std::string key = to_lower(it.key());
        if (key.find("windows") != std::string::npos || key.find("x86_64") != std::string::npos) {
          const auto& pobj = it.value();
          if (pobj.contains("url") && pobj["url"].is_string()) {
            info.download_url = pobj["url"].get<std::string>();
          }
          if (pobj.contains("signature") && pobj["signature"].is_string()) {
            info.signature = pobj["signature"].get<std::string>();
          }
          if (pobj.contains("sha256") && pobj["sha256"].is_string()) {
            info.sha256 = to_lower(pobj["sha256"].get<std::string>());
          }
          break;
        }
      }
      // If no matching platform was chosen, fallback to first entry
      if (info.download_url.empty() && !platforms.empty()) {
        const auto& pobj = platforms.begin().value();
        if (pobj.contains("url") && pobj["url"].is_string()) {
          info.download_url = pobj["url"].get<std::string>();
        }
        if (pobj.contains("signature") && pobj["signature"].is_string()) {
          info.signature = pobj["signature"].get<std::string>();
        }
      }
    }

    // Direct url fallback
    if (info.download_url.empty()) {
      if (j.contains("download_url") && j["download_url"].is_string()) {
        info.download_url = j["download_url"].get<std::string>();
      } else if (j.contains("url") && j["url"].is_string()) {
        info.download_url = j["url"].get<std::string>();
      }
    }
    if (info.signature.empty() && j.contains("signature") && j["signature"].is_string()) {
      info.signature = j["signature"].get<std::string>();
    }
    if (info.sha256.empty() && j.contains("sha256") && j["sha256"].is_string()) {
      info.sha256 = to_lower(j["sha256"].get<std::string>());
    }

    info.has_update = is_version_newer(info.latest_version, info.current_version);
    return Result<AppUpdateInfo>::ok(info);
  } catch (const std::exception& e) {
    return Result<AppUpdateInfo>::err(make_err(std::string("failed to parse manifest JSON: ") + e.what()));
  }
}

Result<AppUpdateInfo> ClientUpdater::check_app_update() {
  if (!downloader_) {
    return Result<AppUpdateInfo>::err(make_err("downloader not configured"));
  }
  const std::string manifest_url =
      // src: tauri.conf.json:31-35（端点结构 1:1）
      //   §5.1 品牌位 + §5.3 自家 0.1.x 版本线：仓库/分支改指 Clash Win 自身。
      //   保留 Mini 原值会让 0.1.0 客户端读到 Mini 3.2.2 清单并下载安装 Clash Mini。
      "https://raw.githubusercontent.com/qiu-yuxiao/clash-win/main/updater/app-update.json";
  auto res = downloader_->fetch_manifest(manifest_url);
  if (!res.is_ok()) {
    return Result<AppUpdateInfo>::err(res.error());
  }
  return parse_manifest(res.value(), current_version_);
}

Result<std::wstring> ClientUpdater::download_app_update(const std::string& download_url,
                                                        const std::string& expected_sha256,
                                                        const std::string& signature_b64) {
  if (!downloader_) {
    return Result<std::wstring>::err(make_err("downloader not configured"));
  }
  wchar_t temp_dir[MAX_PATH];
  GetTempPathW(MAX_PATH, temp_dir);
  std::wstring dest_path = std::wstring(temp_dir) + L"clashwin_setup_update.exe";

  // src: use-client-update.ts:65-67 / locales/zh/layout.json:71 「正在下载更新...」
  emit_client_progress("downloading", 0, "开始下载软件更新包...");

  int last_pct = -1;
  int last_mb_without_total = -1;
  auto on_dl_progress = [&last_pct, &last_mb_without_total](size_t downloaded,
                                                           size_t total) {
    if (total == 0) {
      int mb = static_cast<int>(downloaded / (1024 * 1024));
      if (mb != last_mb_without_total) {
        last_mb_without_total = mb;
        // src: locales/zh/layout.json:74 「已下载 {{downloaded}} MB」
        emit_client_progress(
            "downloading", 0,
            "已下载 " + format_mb(static_cast<double>(downloaded)) + " MB");
      }
      return;
    }
    // src: use-client-update.ts:82 Math.round（四舍五入，非截断）
    int pct = static_cast<int>(std::lround(
        (static_cast<double>(downloaded) / static_cast<double>(total)) * 100.0));
    if (pct > 100) pct = 100;
    if (pct != last_pct) {
      last_pct = pct;
      // src: locales/zh/layout.json:73 「已下载 {{percent}}% ({{downloaded}} MB / {{total}} MB)」
      emit_client_progress(
          "downloading", pct,
          "已下载 " + std::to_string(pct) + "% (" +
              format_mb(static_cast<double>(downloaded)) + " MB / " +
              format_mb(static_cast<double>(total)) + " MB)");
    }
  };

  // BUG-417（← client-update.ts downloadAndInstallClientUpdateWithRetry）：
  //   仅对「网络下载未完成」阶段自动重试（共 3 次，间隔 1500ms / 2500ms）；
  //   下载完成后若 SHA-256 或 Minisign 验签失败，绝不自动重试。
  Result<void> dl_res = Result<void>::err(make_err("not started"));
  for (int attempt = 1; attempt <= kDownloadMaxAttempts; ++attempt) {
    if (attempt > 1) {
      last_pct = -1;
      last_mb_without_total = -1;
      emit_client_progress("downloading", 0, "开始下载软件更新包...");
    }
    dl_res = downloader_->download_installer(download_url, dest_path, on_dl_progress);
    if (dl_res.is_ok()) break;

    std::error_code rm_ec;
    std::filesystem::remove(dest_path, rm_ec);
    if (attempt < kDownloadMaxAttempts) {
      const uint32_t wait_ms = download_retry_delay_ms(attempt);
      CWLOG_ERROR("[ClientUpdate] 下载安装包中断 (第 " + std::to_string(attempt) + "/" +
                  std::to_string(kDownloadMaxAttempts) + " 次): " +
                  dl_res.error().message + "，" + std::to_string(wait_ms) +
                  "ms 后自动重试");
      sleep_for_retry(wait_ms);
    } else {
      CWLOG_ERROR("[ClientUpdate] 客户端升级下载连续 " +
                  std::to_string(kDownloadMaxAttempts) + " 次失败: " +
                  dl_res.error().message);
    }
  }
  if (!dl_res.is_ok()) {
    // src: locales/zh/layout.json:126 「更新失败: {{message}}」
    emit_client_progress("error", 0, "更新失败: " + dl_res.error().message);
    return Result<std::wstring>::err(dl_res.error());
  }

  // 完整性 + 验签（← plugin-updater：先验签后安装；失败可见报错，不静默放行，且不自动重试）
  // 验签输入为安装包原始字节（minisign 预哈希变体内部做 BLAKE2b-512）
  std::vector<uint8_t> file_bytes;
  {
    std::ifstream f(dest_path, std::ios::binary);
    if (f)
      file_bytes.assign(std::istreambuf_iterator<char>(f),
                        std::istreambuf_iterator<char>());
  }
  const std::string actual_sha = compute_sha256_hex(dest_path);
  const bool hash_ok =
      expected_sha256.empty() || actual_sha == to_lower(expected_sha256);
  std::string sig_err;
  const bool sig_ok = verify_installer_signature(file_bytes, signature_b64, &sig_err);
  if (!hash_ok || !sig_ok) {
    std::error_code ec;
    std::filesystem::remove(dest_path, ec);
    const std::string reason =
        !hash_ok ? std::string("安装包校验失败（SHA-256 不匹配）") : sig_err;
    CWLOG_ERROR("[ClientUpdate] 安装包已下载完成但校验失败（不自动重试）: " +
                reason);
    emit_client_progress("error", 0, "更新失败: " + reason);
    return Result<std::wstring>::err(make_err(reason));
  }

  // src: locales/zh/layout.json:75 「下载完成，正在准备安装并重启...」
  emit_client_progress("done", 100, "下载完成，正在准备安装并重启...");
  return Result<std::wstring>::ok(dest_path);
}

// ← tauri-plugin-updater 2.10.1 updater.rs:787-854 install_inner（NSIS 分支）：
//   参数 = install_mode.nsis_args()（Mini installMode=basicUi → 空数组，
//   plugins-workspace updater-v2.10.1 src/config.rs:36-45）+ "/UPDATE" + "/ARGS"
//   + 当前进程参数[1..]；ShellExecuteW 启动后 std::process::exit(0) 立即退出应用
//   ——因此 Mini 的 clientInstallCompleted 通知在真实更新流中不可达（进程先退出）。
//   调用方（main_window）在成功启动后同步退出进程，保持同一用户可见行为。
Result<void> ClientUpdater::install_app_update(const std::wstring& installer_path,
                                               bool silent) {
  (void)silent;  // Mini 无静默分支（basicUi 走完整向导 + /UPDATE 更新模式）
  std::error_code ec;
  if (!std::filesystem::exists(installer_path, ec)) {
    return Result<void>::err(make_err("installer file does not exist"));
  }

  // 当前进程参数[1..]（← current_exe_args()[1..]，updater.rs:797-806）
  std::wstring own_args;
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv) {
    for (int i = 1; i < argc; ++i) {
      if (!own_args.empty()) own_args += L" ";
      own_args += argv[i];
    }
    LocalFree(argv);
  }
  const std::wstring params =
      own_args.empty() ? std::wstring(L"/UPDATE /ARGS")
                       : L"/UPDATE /ARGS " + own_args;

  SHELLEXECUTEINFOW sei{};
  sei.cbSize = sizeof(sei);
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;
  sei.lpVerb = L"open";
  sei.lpFile = installer_path.c_str();
  sei.lpParameters = params.c_str();
  sei.nShow = SW_SHOWNORMAL;

  if (!ShellExecuteExW(&sei)) {
    return Result<void>::err(make_err("failed to launch installer process"));
  }

  if (sei.hProcess) {
    CloseHandle(sei.hProcess);
  }
  return Result<void>::ok();
}

}  // namespace clashwin::update

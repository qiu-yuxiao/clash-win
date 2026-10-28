#include "system/diagnostic_bundle.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "config/config.h"
#include "clashwin/config/constants.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/str_utils.h"
#include "util/utf.h"
#include "yaml-cpp/yaml.h"

namespace clashwin::system {

namespace {

using clashwin::utf::utf16_to_utf8;
using clashwin::utf::utf8_to_utf16;
using util::to_lower;

inline Error make_err(const std::string& msg, int code = 1) {
  return Error{code, msg};
}

constexpr size_t kMaxLogFileReadBytes = 2 * 1024 * 1024;  // 2MB
constexpr size_t kMaxDiagnosticLogFiles = 60;

const char* kBundleReadme =
    "Clash Mini 诊断包\n"
    "=================\n\n"
    "本包由「设置 → 帮助 → 导出诊断包」生成，用于问题排查。\n\n"
    "内容：\n"
    "  logs/                     最近的轮转日志：应用主日志 + service/sidecar 内核日志\n"
    "  runtime-config.redacted.yaml  内核运行时配置（已脱敏）\n"
    "  verge.yaml.redacted.yaml      应用配置（已脱敏）\n"
    "  profiles.yaml.redacted.yaml   订阅配置（已脱敏）\n"
    "  system-info.txt           版本与运行摘要\n\n"
    "脱敏说明：\n"
    "  配置文件已整体脱敏；日志正文已按敏感字段名（password / token / secret /\n"
    "  uuid 等）与 URL 用户信息脱敏。节点密码 / UUID / 密钥 / 订阅链接 / 内核\n"
    "  secret 均替换为 ***REDACTED***。若仍担心，可自行解压后检查再发出。\n\n"
    "排查建议：\n"
    "  断流请重点看 logs/ 中时间点附近的以下关键字：\n"
    "    「连续 2 次检测失败」  自愈触发\n"
    "    「自愈选点」          选点过程与结果\n"
    "    「所有测速节点均不可达」  节点侧或测速链路异常\n"
    "    「测速链路不可用」    测速失真（非节点死亡）\n"
    "    「失败服务器避让」    因服务器间歇不可达而跳过该服务器\n";

uint32_t compute_crc32(const void* data, size_t length) {
  static const uint32_t crc_table[256] = {
      0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
      0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
      0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
      0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
      0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
      0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
      0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
      0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
      0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
      0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
      0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
      0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
      0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
      0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
      0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
      0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
      0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
      0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
      0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
      0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
      0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
      0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
      0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
      0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
      0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
      0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
      0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
      0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
      0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
      0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
      0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
      0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
      0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
      0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
      0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
      0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
      0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
      0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
      0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
      0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
      0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
      0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
      0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d};
  const uint8_t* p = static_cast<const uint8_t*>(data);
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc = (crc >> 8) ^ crc_table[(crc ^ p[i]) & 0xFF];
  }
  return crc ^ 0xFFFFFFFF;
}

}  // namespace

std::string DiagnosticBundle::redact_log_text(const std::string& text) {
  // Precedence order:
  // Rule 0a: Authorization header line
  static const std::regex rule_0a(R"((\bauthorization\s*[=:]\s*)[^\r\n]+)",
                                  std::regex_constants::icase);
  // Rule 0b: Bare Bearer token
  static const std::regex rule_0b(R"((\bbearer\s+)[^\s"&,;]+)",
                                  std::regex_constants::icase);
  // Rule 1: URL user credentials user:pass@host
  static const std::regex rule_1(R"(((?:[a-z][a-z0-9+.-]*://)?)[^\s@/:]+:[^\s@/:]+@)",
                                 std::regex_constants::icase);
  // Rule 2: Key-value secrets
  static const std::regex rule_2(
      R"((["']?\b(?:obfs-password|auth-str|password|passwd|secret|token|uuid|auth|sign)\b["']?)(\s*[=:]\s*["']?)(?:(bearer\s+))?(?!(?:bearer\b|\*\*\*REDACTED\*\*\*))[^\s"&,;]+(["']?))",
      std::regex_constants::icase);

  std::string no_auth = std::regex_replace(text, rule_0a, "$1***REDACTED***");
  std::string no_bearer = std::regex_replace(no_auth, rule_0b, "$1***REDACTED***");
  std::string no_userinfo = std::regex_replace(no_bearer, rule_1, "$1***REDACTED***@");
  std::string no_kv = std::regex_replace(no_userinfo, rule_2, "$1$2$3***REDACTED***$4");
  return no_kv;
}

std::string DiagnosticBundle::redact_yaml_text(const std::string& yaml_text) {
  try {
    YAML::Node root = YAML::Load(yaml_text);
    if (!root.IsDefined() || root.IsNull()) {
      return yaml_text;
    }

    std::function<void(YAML::Node)> walk = [&](YAML::Node node) {
      if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
          if (it->first.IsScalar()) {
            std::string k = to_lower(it->first.as<std::string>());
            static const std::unordered_set<std::string> sensitive = {
                "password", "uuid", "psk", "auth", "auth-str", "authorization",
                "obfs-password", "secret", "token", "url", "private-key", "public-key"};
            if (sensitive.count(k) > 0 && it->second.IsScalar()) {
              it->second = "***REDACTED***";
            } else {
              walk(it->second);
            }
          } else {
            walk(it->second);
          }
        }
      } else if (node.IsSequence()) {
        for (size_t i = 0; i < node.size(); ++i) {
          walk(node[i]);
        }
      }
    };

    walk(root);
    YAML::Emitter out;
    out << root;
    std::string res = out.c_str();
    if (res.empty() || res.back() != '\n') res += '\n';
    return res;
  } catch (const std::exception& e) {
    return std::string("# 配置解析失败，为避免泄露原始内容，本文件不含配置正文: ") +
           e.what() + "\n";
  }
}

std::string DiagnosticBundle::tail_truncate_log(const std::string& text, size_t max_bytes) {
  if (text.size() <= max_bytes) {
    return text;
  }

  std::string tail = text.substr(text.size() - max_bytes);
  size_t nl = tail.find('\n');
  size_t valid_start = (nl != std::string::npos) ? (nl + 1) : 0;
  std::string valid_tail = tail.substr(valid_start);

  double total_mb = static_cast<double>(text.size()) / (1024.0 * 1024.0);
  double kept_mb = static_cast<double>(text.size() - (text.size() - max_bytes + valid_start)) /
                   (1024.0 * 1024.0);

  char notice[256];
  sprintf_s(notice,
            "[... 日志文件较大（总计 %.2f MB），诊断包已自动截取最新 %.2f MB 内容 ...]\n",
            total_mb, kept_mb);
  return std::string(notice) + valid_tail;
}

std::string DiagnosticBundle::collect_system_info() {
  time_t now = time(nullptr);
  tm local_tm{};
  localtime_s(&local_tm, &now);
  char time_buf[64];
  strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &local_tm);

  auto verge = config::Config::instance().verge().data_arc();
  auto clash = config::Config::instance().clash().data_arc();
  auto clash_info = clash->get_client_info();
  uint32_t clash_ver = 2;
  if (clash->mapping()["config_version"] && clash->mapping()["config_version"].IsScalar()) {
    try {
      clash_ver = clash->mapping()["config_version"].as<uint32_t>();
    } catch (...) {}
  }

  int node_count = 0;
  std::wstring cfg_path = util::dirs::clash_mini_path();
  std::ifstream cf(cfg_path);
  if (cf.is_open()) {
    std::string content((std::istreambuf_iterator<char>(cf)), std::istreambuf_iterator<char>());
    cf.close();
    try {
      YAML::Node n = YAML::Load(content);
      if (n["proxies"] && n["proxies"].IsSequence()) {
        node_count = static_cast<int>(n["proxies"].size());
      }
    } catch (...) {}
  }

  std::ostringstream ss;
  ss << "生成时间        : " << time_buf << "\n"
     << "应用版本        : 3.2.2\n"
     << "操作系统        : windows / x86_64\n"
     << "verge 配置版本  : " << (verge->config_version.has_value() ? std::to_string(*verge->config_version) : "null")
     << "（程序支持 " << config::consts::kCurrentVergeVersion << "）\n"
     << "clash 配置版本  : " << clash_ver
     << "（程序支持 " << config::consts::kCurrentClashVersion << "）\n"
     << "TUN 模式        : " << (verge->enable_tun_mode.value_or(false) ? "true" : "false") << "\n"
     << "系统代理        : " << (verge->enable_system_proxy.value_or(false) ? "true" : "false") << "\n"
     << "Allow IPv6      : " << (clash_info.ipv6 ? "true" : "false") << "\n"
     << "规则兜底        : " << (verge->rule_fallback.has_value() ? *verge->rule_fallback : "") << "\n"
     << "日志级别        : " << (verge->app_log_level.has_value() ? *verge->app_log_level : "") << "\n"
     << "日志轮转        : " << verge->app_log_max_size.value_or(128) << " KB × " << verge->app_log_max_count.value_or(40) << " 个\n"
     << "节点总数        : " << node_count << "\n";
  return ss.str();
}

Result<void> DiagnosticBundle::write_zip_file(const std::wstring& zip_path,
                                              const std::vector<ZipEntry>& entries) {
  std::ofstream out(zip_path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    return Result<void>::err(make_err("failed to open output zip file"));
  }

  struct CentralEntryInfo {
    std::string name;
    uint32_t crc = 0;
    uint32_t comp_size = 0;
    uint32_t uncomp_size = 0;
    uint32_t local_header_offset = 0;
  };

  std::vector<CentralEntryInfo> cd_entries;
  cd_entries.reserve(entries.size());

  for (const auto& entry : entries) {
    uint32_t offset = static_cast<uint32_t>(out.tellp());
    uint32_t crc = compute_crc32(entry.content.data(), entry.content.size());
    uint32_t size = static_cast<uint32_t>(entry.content.size());
    uint16_t name_len = static_cast<uint16_t>(entry.rel_path.size());

    // Local file header: 30 bytes
    uint32_t sig = 0x04034b50;
    uint16_t ver = 20;
    uint16_t flags = 0x0800;  // UTF-8
    uint16_t method = 0;       // STORE
    uint16_t mod_time = 0;
    uint16_t mod_date = 0;
    uint16_t extra_len = 0;

    out.write(reinterpret_cast<const char*>(&sig), 4);
    out.write(reinterpret_cast<const char*>(&ver), 2);
    out.write(reinterpret_cast<const char*>(&flags), 2);
    out.write(reinterpret_cast<const char*>(&method), 2);
    out.write(reinterpret_cast<const char*>(&mod_time), 2);
    out.write(reinterpret_cast<const char*>(&mod_date), 2);
    out.write(reinterpret_cast<const char*>(&crc), 4);
    out.write(reinterpret_cast<const char*>(&size), 4);
    out.write(reinterpret_cast<const char*>(&size), 4);
    out.write(reinterpret_cast<const char*>(&name_len), 2);
    out.write(reinterpret_cast<const char*>(&extra_len), 2);
    out.write(entry.rel_path.data(), name_len);
    if (!entry.content.empty()) {
      out.write(entry.content.data(), size);
    }

    cd_entries.push_back({entry.rel_path, crc, size, size, offset});
  }

  uint32_t cd_offset = static_cast<uint32_t>(out.tellp());

  // Write Central Directory headers
  for (const auto& cd : cd_entries) {
    uint32_t sig = 0x02014b50;
    uint16_t ver_made = 20;
    uint16_t ver_need = 20;
    uint16_t flags = 0x0800;
    uint16_t method = 0;
    uint16_t mod_time = 0;
    uint16_t mod_date = 0;
    uint16_t name_len = static_cast<uint16_t>(cd.name.size());
    uint16_t extra_len = 0;
    uint16_t comment_len = 0;
    uint16_t disk_start = 0;
    uint16_t int_attr = 0;
    uint32_t ext_attr = 0;

    out.write(reinterpret_cast<const char*>(&sig), 4);
    out.write(reinterpret_cast<const char*>(&ver_made), 2);
    out.write(reinterpret_cast<const char*>(&ver_need), 2);
    out.write(reinterpret_cast<const char*>(&flags), 2);
    out.write(reinterpret_cast<const char*>(&method), 2);
    out.write(reinterpret_cast<const char*>(&mod_time), 2);
    out.write(reinterpret_cast<const char*>(&mod_date), 2);
    out.write(reinterpret_cast<const char*>(&cd.crc), 4);
    out.write(reinterpret_cast<const char*>(&cd.comp_size), 4);
    out.write(reinterpret_cast<const char*>(&cd.uncomp_size), 4);
    out.write(reinterpret_cast<const char*>(&name_len), 2);
    out.write(reinterpret_cast<const char*>(&extra_len), 2);
    out.write(reinterpret_cast<const char*>(&comment_len), 2);
    out.write(reinterpret_cast<const char*>(&disk_start), 2);
    out.write(reinterpret_cast<const char*>(&int_attr), 2);
    out.write(reinterpret_cast<const char*>(&ext_attr), 4);
    out.write(reinterpret_cast<const char*>(&cd.local_header_offset), 4);
    out.write(cd.name.data(), name_len);
  }

  uint32_t cd_size = static_cast<uint32_t>(out.tellp()) - cd_offset;

  // End of central directory record (EOCD): 22 bytes
  uint32_t eocd_sig = 0x06054b50;
  uint16_t disk_num = 0;
  uint16_t start_disk = 0;
  uint16_t total_entries_disk = static_cast<uint16_t>(cd_entries.size());
  uint16_t total_entries = static_cast<uint16_t>(cd_entries.size());
  uint16_t comment_len = 0;

  out.write(reinterpret_cast<const char*>(&eocd_sig), 4);
  out.write(reinterpret_cast<const char*>(&disk_num), 2);
  out.write(reinterpret_cast<const char*>(&start_disk), 2);
  out.write(reinterpret_cast<const char*>(&total_entries_disk), 2);
  out.write(reinterpret_cast<const char*>(&total_entries), 2);
  out.write(reinterpret_cast<const char*>(&cd_size), 4);
  out.write(reinterpret_cast<const char*>(&cd_offset), 4);
  out.write(reinterpret_cast<const char*>(&comment_len), 2);

  out.close();
  return Result<void>::ok();
}

Result<std::string> DiagnosticBundle::export_diagnostic_bundle() {
  std::wstring home_dir = util::dirs::app_home_dir();
  std::filesystem::path diag_dir = std::filesystem::path(home_dir) / "diagnostics";
  std::error_code ec;
  std::filesystem::create_directories(diag_dir, ec);
  if (ec) {
    return Result<std::string>::err(make_err("failed to create diagnostics directory: " + ec.message()));
  }

  time_t now = time(nullptr);
  tm local_tm{};
  localtime_s(&local_tm, &now);
  char stamp[64];
  strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local_tm);

  std::filesystem::path zip_path = diag_dir / ("clash-mini-diag-" + std::string(stamp) + ".zip");
  std::vector<ZipEntry> entries;

  // 1) Logs: recursively collect, skip symlinks, sort mtime desc, cap at 60 files, tail-cap 2MB
  std::filesystem::path logs_dir = std::filesystem::path(home_dir) / "logs";
  if (std::filesystem::exists(logs_dir, ec)) {
    struct LogItem {
      std::filesystem::path path;
      std::filesystem::file_time_type mtime;
    };
    std::vector<LogItem> collected;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(logs_dir, ec)) {
      if (std::filesystem::is_symlink(entry.symlink_status())) {
        continue;
      }
      if (entry.is_regular_file()) {
        if (entry.path().extension() == ".log") {
          auto mt = entry.last_write_time(ec);
          if (!ec) {
            collected.push_back({entry.path(), mt});
          }
        }
      }
    }

    std::sort(collected.begin(), collected.end(),
              [](const LogItem& a, const LogItem& b) { return a.mtime > b.mtime; });

    size_t count = 0;
    for (const auto& item : collected) {
      if (count >= kMaxDiagnosticLogFiles) break;

      std::ifstream lf(item.path, std::ios::binary);
      if (lf.is_open()) {
        std::string raw((std::istreambuf_iterator<char>(lf)), std::istreambuf_iterator<char>());
        lf.close();

        std::string truncated = tail_truncate_log(raw, kMaxLogFileReadBytes);
        std::string redacted = redact_log_text(truncated);

        auto rel = std::filesystem::relative(item.path, logs_dir, ec);
        std::string rel_str = rel.string();
        std::replace(rel_str.begin(), rel_str.end(), '\\', '/');

        entries.push_back({"logs/" + rel_str, redacted});
        count++;
      }
    }
  }

  // 2) Configurations
  const std::vector<std::pair<std::wstring, std::string>> cfgs = {
      {util::dirs::clash_mini_path(), "runtime-config.redacted.yaml"},
      {util::dirs::verge_path(), "verge.yaml.redacted.yaml"},
      {util::dirs::profiles_path(), "profiles.yaml.redacted.yaml"},
  };

  for (const auto& [path, out_name] : cfgs) {
    std::ifstream f(path);
    if (f.is_open()) {
      std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      f.close();
      entries.push_back({out_name, redact_yaml_text(text)});
    }
  }

  // 3) System Info
  entries.push_back({"system-info.txt", collect_system_info()});

  // 4) README.txt
  entries.push_back({"README.txt", kBundleReadme});

  auto write_res = write_zip_file(zip_path.wstring(), entries);
  if (!write_res.is_ok()) {
    return Result<std::string>::err(write_res.error());
  }

  return Result<std::string>::ok(zip_path.string());
}

}  // namespace clashwin::system

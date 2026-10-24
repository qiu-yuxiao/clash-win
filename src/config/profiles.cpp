#include "profiles.h"

#include <windows.h>

#include <fstream>
#include <set>

#include "clashwin/config/constants.h"
#include "migrate.h"
#include "prfitem.h"
#include "util/dirs.h"
#include "util/log.h"
#include "util/utf.h"
#include "yaml_serde.h"

// 布局说明：IProfiles 成员定义在 clashwin::config；自由函数在
// clashwin::config::profiles（成员侧以 profiles:: 限定名访问）。
namespace clashwin::config::profiles {

namespace {

// 摘除 uid 项并返回其 file 名
std::optional<std::string> take_item_file_by_uid(std::vector<PrfItem>& items,
                                                 const std::string& target) {
  for (size_t i = 0; i < items.size(); ++i) {
    if (items[i].uid && *items[i].uid == target) {
      std::optional<std::string> f = items[i].file;
      items.erase(items.begin() + static_cast<long>(i));
      return f;
    }
  }
  return std::nullopt;
}

void remove_profile_file(const std::optional<std::string>& file) {
  if (!file) return;
  std::wstring path =
      prfitem::profiles_dir() + L"\\" + utf::utf8_to_utf16(*file);
  DeleteFileW(path.c_str());
}

}  // namespace

std::wstring profiles_path() { return util::dirs::profiles_path(); }

}  // namespace clashwin::config::profiles

// ---------- IProfiles 成员定义（clashwin::config） ----------
namespace clashwin::config {

// ---------- 序列化（字段顺序 = profiles.rs 声明顺序） ----------
YAML::Node IProfiles::to_yaml() const {
  YAML::Node m(YAML::NodeType::Map);
  config_version ? serde::set_u32(m, "config_version", *config_version)
                 : serde::set_null(m, "config_version");
  current ? serde::set_str(m, "current", *current)
          : serde::set_null(m, "current");
  if (items) {
    YAML::Node seq(YAML::NodeType::Sequence);
    for (const auto& it : *items) seq.push_back(it.to_yaml());
    m["items"] = seq;
  } else {
    serde::set_null(m, "items");
  }
  return m;
}

IProfiles IProfiles::from_yaml(const YAML::Node& m) {
  IProfiles p;
  if (!m || !m.IsMap()) return p;
  p.config_version = serde::opt_u32(m, "config_version");
  p.current = serde::opt_str(m, "current");
  const YAML::Node its = m["items"];
  if (its && its.IsSequence()) {
    std::vector<PrfItem> vec;
    for (const auto& it : its) {
      auto item = PrfItem::from_yaml(it);
      if (item) vec.push_back(std::move(*item));
    }
    p.items = std::move(vec);
  }
  return p;
}

Result<void> IProfiles::save_file() const {
  return profiles::save_profiles(*this);
}

// ---------- load（← profiles.rs::new） ----------
IProfiles IProfiles::load() {
  auto node = serde::read_mapping_file(profiles::profiles_path());
  if (!node.is_ok()) {
    // 文件缺失/损坏 → default。与 Rust 版差异：这里直接把 config_version
    // 落到当前版本（1）——Rust 在此路径返回 None，首启退出落盘会写出
    // `config_version: null`，第二次启动才被读回为 1；T03 完成标准要求
    // 首启产物与实机一致（实机为 1），故在装载期归一。
    CWLOG_ERROR("failed to load profiles.yaml: " + node.error().message);
    IProfiles fresh;
    fresh.config_version = consts::kCurrentProfilesVersion;
    return fresh;
  }
  IProfiles profiles = IProfiles::from_yaml(node.value());

  // 版本迁移（备份成功才迁移，与 verge 对称）
  uint32_t from_version =
      profiles.config_version.value_or(consts::kImplicitLegacyVersion);
  std::wstring path = profiles::profiles_path();
  if (from_version < migrate::current_profiles_version()) {
    auto backup = migrate::backup_before_migration(path, from_version);
    if (!backup) {
      CWLOG_WARN("profiles.yaml migration backup failed, skip migration");
    } else {
      uint32_t migrated = migrate::migrate_profiles(profiles, from_version);
      profiles.config_version = migrated;
      if (migrated != from_version) {
        auto sv = profiles::save_profiles(profiles);
        if (!sv.is_ok())
          CWLOG_WARN("profiles.yaml migration save failed: " +
                     sv.error().message);
      }
    }
  } else {
    // ← profiles.rs:85：读取成功即落当前版本号（后续保存写出 1）
    profiles.config_version = from_version;
  }

  // 补缺失 uid（profiles.rs:97-102）
  if (!profiles.items) profiles.items = std::vector<PrfItem>{};
  for (auto& item : *profiles.items) {
    if (!item.uid) item.uid = prfitem::gen_uid_prefixed("d");
  }
  return profiles;
}

// ---------- 查询 ----------
const PrfItem* IProfiles::get_item(const std::string& uid) const {
  if (!items) return nullptr;
  for (const auto& each : *items) {
    if (each.uid && *each.uid == uid) return &each;
  }
  return nullptr;
}

PrfItem* IProfiles::get_item_mut(const std::string& uid) {
  if (!items) return nullptr;
  for (auto& each : *items) {
    if (each.uid && *each.uid == uid) return &each;
  }
  return nullptr;
}

// ---------- patch_config（← profiles.rs:117-130：仅修 current） ----------
void IProfiles::patch_config(const IProfiles& patch) {
  if (!items) items = std::vector<PrfItem>{};
  if (patch.current) {
    for (const auto& e : *items) {
      if (e.uid && *e.uid == *patch.current) {
        current = patch.current;
        break;
      }
    }
  }
}

// ---------- append_item（← profiles.rs:162-238） ----------
Result<void> IProfiles::append_item(PrfItem& item) {
  if (!items) items = std::vector<PrfItem>{};

  // remote + url 已存在 → 复用既有 uid/file（去重）
  if (item.itype && *item.itype == "remote" && item.url) {
    for (auto& e : *items) {
      if (e.itype && *e.itype == "remote" && e.url && *e.url == *item.url) {
        item.uid = e.uid;
        item.file = e.file;
        break;
      }
    }
  }

  if (!item.uid) {
    Error e;
    e.code = 21;
    e.message = "the uid should not be null";
    return Result<void>::err(std::move(e));
  }

  // 同 uid → 更新元数据（option 走 merge 语义）
  for (auto& pos : *items) {
    if (!(pos.uid && *pos.uid == *item.uid)) continue;
    pos.updated = item.updated;
    pos.extra = item.extra;
    if (item.name) pos.name = item.name;
    if (item.desc) pos.desc = item.desc;
    if (item.home) pos.home = item.home;
    pos.option = PrfOption::merge_opt(pos.option, item.option);
    if (item.file_data) {
      if (!pos.file) {
        Error e;
        e.code = 22;
        e.message = "file field is required in existing item";
        return Result<void>::err(std::move(e));
      }
      std::wstring path = prfitem::profiles_dir() + L"\\" +
                          utf::utf8_to_utf16(*pos.file);
      auto r = prfitem::write_file_if_changed(path, *item.file_data);
      if (!r.is_ok()) return r;
    }
    return Result<void>::ok();
  }

  // 新项：先落文件数据
  if (item.file_data) {
    if (!item.file) {
      Error e;
      e.code = 23;
      e.message = "the file should not be null";
      return Result<void>::err(std::move(e));
    }
    std::wstring path =
        prfitem::profiles_dir() + L"\\" + utf::utf8_to_utf16(*item.file);
    auto r = prfitem::write_file_if_changed(path, *item.file_data);
    if (!r.is_ok()) return r;
  }

  // 首个 remote/local 项自动成为 current
  if (!current && item.itype &&
      (*item.itype == "remote" || *item.itype == "local")) {
    current = item.uid;
  }
  items->push_back(item);
  return Result<void>::ok();
}

// ---------- delete_item（← profiles.rs:337-382） ----------
Result<bool> IProfiles::delete_item(const std::string& uid) {
  std::string current_uid = current.value_or(uid);
  // 收集 option 关联子项 uid（连删）
  std::vector<std::string> delete_uids;
  if (const PrfItem* item = get_item(uid)) {
    if (item->option) {
      const PrfOption& op = *item->option;
      for (const auto& sub :
           {op.merge, op.script, op.rules, op.proxies, op.groups}) {
        if (sub) delete_uids.push_back(*sub);
      }
    }
  } else {
    Error e;
    e.code = 24;
    e.message = "failed to get the profile item \"uid:" + uid + "\"";
    return Result<bool>::err(std::move(e));
  }

  if (!items) items = std::vector<PrfItem>{};

  // 删主项及其文件
  if (auto f = profiles::take_item_file_by_uid(*items, uid))
    profiles::remove_profile_file(f);
  // 删关联子项
  for (const auto& du : delete_uids) {
    if (auto f = profiles::take_item_file_by_uid(*items, du))
      profiles::remove_profile_file(f);
  }

  // 删 current → 顺延到首个 remote/local
  if (current_uid == uid) {
    current = std::nullopt;
    for (const auto& item : *items) {
      if (item.itype && (*item.itype == "remote" || *item.itype == "local")) {
        current = item.uid;
        break;
      }
    }
  }

  auto r = save_file();
  if (!r.is_ok()) return Result<bool>::err(r.error());
  return Result<bool>::ok(current_uid == uid);
}

// ---------- reorder（← profiles.rs:241-263） ----------
Result<void> IProfiles::reorder(const std::string& active_id,
                                const std::string& over_id) {
  if (!items) items = std::vector<PrfItem>{};
  std::optional<size_t> old_index, new_index;
  for (size_t i = 0; i < items->size(); ++i) {
    const auto& e = (*items)[i];
    if (e.uid && *e.uid == active_id) old_index = i;
    if (e.uid && *e.uid == over_id) new_index = i;
  }
  if (!old_index || !new_index) return Result<void>::ok();
  PrfItem item = (*items)[*old_index];
  items->erase(items->begin() + static_cast<long>(*old_index));
  items->insert(items->begin() + static_cast<long>(*new_index), std::move(item));
  return save_file();
}

// ---------- patch_item（← profiles.rs:266-289；file 禁改——路径穿越防护） ----------
Result<void> IProfiles::patch_item(const std::string& uid, const PrfItem& item) {
  if (!items) items = std::vector<PrfItem>{};
  for (auto& each : *items) {
    if (!(each.uid && *each.uid == uid)) continue;
    if (item.itype) each.itype = item.itype;
    if (item.name) each.name = item.name;
    if (item.desc) each.desc = item.desc;
    // 安全修复：禁止经 IPC 修改 file 字段（防止路径穿越），file 由内部维护
    if (item.url) each.url = item.url;
    if (item.selected) each.selected = item.selected;
    if (item.extra) each.extra = item.extra;
    if (item.updated) each.updated = item.updated;
    if (item.option) each.option = item.option;
    return save_file();
  }
  Error e;
  e.code = 25;
  e.message = "failed to find the profile item \"uid:" + uid + "\"";
  return Result<void>::err(std::move(e));
}

// ---------- cleanup_orphaned_files（← profiles.rs:437-506） ----------
Result<void> IProfiles::cleanup_orphaned_files() const {
  std::wstring dir = prfitem::profiles_dir();
  WIN32_FIND_DATAW fd;
  std::wstring pattern = dir + L"\\*";
  HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return Result<void>::ok();  // 目录不存在

  // 活跃文件集合（items 全部 file + remote/local 关联子项 file）
  std::set<std::string> active_files;
  if (items) {
    for (const auto& item : *items) {
      if (item.file) active_files.insert(*item.file);
      if (item.itype && (*item.itype == "remote" || *item.itype == "local") &&
          item.option) {
        for (const auto& sub_uid : {item.option->merge, item.option->script,
                                    item.option->rules, item.option->proxies,
                                    item.option->groups}) {
          if (!sub_uid) continue;
          if (const PrfItem* sub = get_item(*sub_uid)) {
            if (sub->file) active_files.insert(*sub->file);
          }
        }
      }
    }
  }

  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    std::string name = utf::utf16_to_utf8(fd.cFileName);
    if (!profiles::is_profile_file(name)) continue;
    if (profiles::is_protected_global_file(name)) continue;  // 保护全局扩展配置
    if (!active_files.count(name)) {
      std::wstring path = dir + L"\\" + fd.cFileName;
      if (!DeleteFileW(path.c_str())) {
        CWLOG_WARN("failed to cleanup orphan file: " + name);
      } else {
        CWLOG_INFO("cleaned orphan profile file: " + name);
      }
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return Result<void>::ok();
}

}  // namespace clashwin::config

// ---------- profiles 命名空间自由函数 ----------
namespace clashwin::config::profiles {

std::string dump_profiles(const IProfiles& p) { return serde::dump(p.to_yaml()); }

Result<void> save_profiles(const IProfiles& p) {
  return serde::save_yaml_text(profiles_path(), dump_profiles(p),
                               "# Profiles Config for Clash Verge");
}

// ---------- 全局保护件（← config.rs:134-145） ----------
Result<void> ensure_default_profile_items(IProfiles& p) {
  if (!p.get_item(kGlobalMergeUid)) {
    PrfItem merge_item = PrfItem::from_merge(std::string(kGlobalMergeUid));
    auto r = p.append_item(merge_item);
    if (!r.is_ok()) return r;
  }
  if (!p.get_item(kGlobalScriptUid)) {
    PrfItem script_item = PrfItem::from_script(std::string(kGlobalScriptUid));
    auto r = p.append_item(script_item);
    if (!r.is_ok()) return r;
  }
  return Result<void>::ok();
}

// append_item_safe：供 prfitem.cpp ensure_enhance_items 使用。
// T03 单线程装配期（Config::init）直接操作全局 IProfiles 实例；
// Config 门面（config.cpp）提供线程安全的 Draft 包装后由 T09 接管。
namespace {
IProfiles* g_profiles_instance = nullptr;
}

void set_profiles_instance(IProfiles* p) { g_profiles_instance = p; }

Result<void> append_item_safe(PrfItem& item) {
  if (!g_profiles_instance) {
    Error e;
    e.code = 26;
    e.message = "profiles instance not initialized";
    return Result<void>::err(std::move(e));
  }
  return g_profiles_instance->append_item(item);
}

// ---------- 文件名规则（← profiles.rs:570-593） ----------
bool is_profile_file(const std::string& filename) {
  // 手写正则等价：^[RL][a-zA-Z0-9]+\.yaml$ / ^m[...]\.yaml$ /
  // ^s[...]\.js$ / ^[rpg][...]\.yaml$
  auto alnum = [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z');
  };
  std::string stem, ext;
  size_t dot = filename.rfind('.');
  if (dot == std::string::npos) return false;
  stem = filename.substr(0, dot);
  ext = filename.substr(dot + 1);
  if (stem.size() < 2) return false;
  char head = stem[0];
  for (size_t i = 1; i < stem.size(); ++i) {
    if (!alnum(stem[i])) return false;
  }
  if ((head == 'R' || head == 'L') && ext == "yaml") return true;
  if (head == 'm' && ext == "yaml") return true;
  if (head == 's' && ext == "js") return true;
  if ((head == 'r' || head == 'p' || head == 'g') && ext == "yaml") return true;
  return false;
}

bool is_protected_global_file(const std::string& filename) {
  // ← profiles.rs::get_protected_global_files
  return filename == "Merge.yaml" || filename == "Script.js";
}

IProfiles load() { return IProfiles::load(); }

}  // namespace clashwin::config::profiles

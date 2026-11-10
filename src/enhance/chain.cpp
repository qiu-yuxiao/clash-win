#include "enhance/chain.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "enhance/enhance_constants.h"
#include "util/utf.h"
#include "util/yaml_utils.h"

namespace clashwin::enhance {

namespace {

bool path_exists(const std::wstring& path) {
  DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::string read_text_file(const std::wstring& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

}  // namespace

ChainItem chain_item_load(const std::wstring& profiles_dir,
                          const std::string& uid, const std::string& itype,
                          const std::string& file) {
  ChainItem item;
  item.uid = uid;
  std::wstring path = profiles_dir;
  if (!path.empty() && path.back() != L'\\' && path.back() != L'/') path += L'\\';
  path += utf::utf8_to_utf16(file);

  if (itype == "script") {
    item.kind = ChainItem::Kind::Script;
    if (!file.empty() && path_exists(path)) {
      std::string text = read_text_file(path);
      // 注意：与 Rust fs::read_to_string(...).ok()? 一致——空文件也视为成功，
      // 但空 script 与默认模板语义不同；此处仅当非空才采用，空回退默认模板
      if (!text.empty()) {
        item.script = text;
        return item;
      }
    }
    item.script = consts::kItemScript;
    return item;
  }

  if (itype == "merge") {
    item.kind = ChainItem::Kind::Merge;
    if (!file.empty() && path_exists(path)) {
      auto loaded = util::load_yaml_file(path);
      if (loaded.is_ok() && loaded.value().IsMap()) {
        item.merge = loaded.value();
        return item;
      }
    }
    item.merge = YAML::Node(YAML::NodeType::Map);
    return item;
  }

  if (itype == "rules" || itype == "proxies") {
    item.kind =
        itype == "rules" ? ChainItem::Kind::Rules : ChainItem::Kind::Proxies;
    auto r = read_seq_map_file(path);
    if (r.ok) {
      item.seq = std::move(r.map);
    } else {
      item.seq = SeqMap::empty();
    }
    return item;
  }

  // 其它 itype：None → 空默认 Merge
  item.kind = ChainItem::Kind::Merge;
  item.merge = YAML::Node(YAML::NodeType::Map);
  return item;
}

std::vector<ChainItem> builtin_chain_items(bool clash_meta_supported) {
  std::vector<ChainItem> items;
  // ← chain.rs::builtin：ChainSupport::ClashMeta 仅支持 "win-mihomo"；core
  // 为 None 时 is_support = true
  if (!clash_meta_supported) return items;
  ChainItem hy;
  hy.uid = "verge_hy_alpn";
  hy.kind = ChainItem::Kind::Script;
  hy.script = consts::kBuiltinScriptHyAlpn;
  items.push_back(std::move(hy));

  ChainItem guard;
  guard.uid = "verge_meta_guard";
  guard.kind = ChainItem::Kind::Script;
  guard.script = consts::kBuiltinScriptMetaGuard;
  items.push_back(std::move(guard));
  return items;
}

}  // namespace clashwin::enhance

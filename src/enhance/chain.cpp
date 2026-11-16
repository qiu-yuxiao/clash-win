#include "enhance/chain.h"

#include <windows.h>

#include <cstdio>

#include "util/utf.h"
#include "util/yaml_utils.h"

namespace clashwin::enhance {

namespace {

bool path_exists(const std::wstring& path) {
  DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
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

  // 历史 itype="script" 或其它未识别类型：安全无操作空默认 Merge
  item.kind = ChainItem::Kind::Merge;
  item.merge = YAML::Node(YAML::NodeType::Map);
  return item;
}

}  // namespace clashwin::enhance

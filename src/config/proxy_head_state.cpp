#include "proxy_head_state.h"

#include <windows.h>

#include <fstream>

#include "clashwin/config/constants.h"
#include "util/dirs.h"
#include "util/json_utils.h"
#include "util/utf.h"

namespace clashwin::config::proxy_head_state {

std::wstring state_path() { return util::dirs::proxy_head_state_path(); }

namespace {

// ← help.rs::save_json：pretty(2) + 内容未变化跳过写 + tmp/MoveFileExW 原子替换
bool read_file(const std::wstring& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign((std::istreambuf_iterator<char>(f)),
             std::istreambuf_iterator<char>());
  return true;
}

Result<void> save_pretty(const std::wstring& path, const std::string& body) {
  std::string existing;
  if (read_file(path, existing) && existing == body) return Result<void>::ok();

  size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) {
    Error e;
    e.code = 30;
    e.message = "failed to get parent directory";
    return Result<void>::err(std::move(e));
  }
  wchar_t pid_buf[16];
  std::swprintf(pid_buf, 16, L"%u", static_cast<unsigned>(GetCurrentProcessId()));
  std::wstring tmp =
      path.substr(0, slash + 1) + path.substr(slash + 1) + L".tmp_" + pid_buf;
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
      Error e;
      e.code = 31;
      e.message = "failed to write temp file \"" + utf::utf16_to_utf8(tmp) + "\"";
      return Result<void>::err(std::move(e));
    }
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
    if (!f) {
      Error e;
      e.code = 31;
      e.message = "failed to write temp file \"" + utf::utf16_to_utf8(tmp) + "\"";
      return Result<void>::err(std::move(e));
    }
  }
  if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    Error e;
    e.code = 32;
    e.message = "failed to save file \"" + utf::utf16_to_utf8(path) + "\"";
    return Result<void>::err(std::move(e));
  }
  return Result<void>::ok();
}

}  // namespace

Result<void> save(const nlohmann::json& state) {
  // serde_json::to_string_pretty：2 空格缩进，无尾换行
  return save_pretty(state_path(), state.is_object() ? state.dump(2)
                                                     : nlohmann::json::object().dump(2));
}

nlohmann::json load() {
  std::string content;
  if (!read_file(state_path(), content)) return nlohmann::json::object();
  try {
    nlohmann::json j = nlohmann::json::parse(content);
    if (j.is_object()) return j;
  } catch (...) {
  }
  // 撕裂/损坏 → 默认空对象（选点范围不能越界）
  return nlohmann::json::object();
}

std::optional<GroupHeadState> get_group_state(const nlohmann::json& state,
                                              const std::string& uid,
                                              const std::string& group) {
  if (!state.is_object()) return std::nullopt;
  auto it_profile = state.find(uid);
  if (it_profile == state.end() || !it_profile->is_object()) return std::nullopt;
  auto it_group = it_profile->find(group);
  if (it_group == it_profile->end() || !it_group->is_object()) return std::nullopt;
  GroupHeadState gs;
  gs.filter_text = util::get_or_string(*it_group, "filterText", "");
  gs.open = util::get_or_bool(*it_group, "open", false);
  gs.show_type = util::get_or_bool(*it_group, "showType", true);
  gs.sort_type = util::get_or_int(*it_group, "sortType", 0);
  return gs;
}

nlohmann::json set_group_state(nlohmann::json state, const std::string& uid,
                               const std::string& group,
                               const GroupHeadState& value) {
  if (!state.is_object()) state = nlohmann::json::object();
  nlohmann::json gs = nlohmann::json::object();
  gs["filterText"] = value.filter_text;
  gs["open"] = value.open;
  gs["showType"] = value.show_type;
  gs["sortType"] = value.sort_type;
  state[uid][group] = std::move(gs);
  return state;
}

}  // namespace clashwin::config::proxy_head_state

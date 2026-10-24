#include "clashwin/config/app_config.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <iostream>
#include <algorithm>

namespace clashwin::config {

static int64_t CurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static std::string Trim(std::string_view s) {
    size_t start = s.find_first_not_of(" \t\r\n\"'");
    if (start == std::string_view::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n\"'");
    return std::string(s.substr(start, end - start + 1));
}

AppConfig& AppConfig::Instance() {
    static AppConfig instance;
    return instance;
}

AppConfig::AppConfig() = default;
AppConfig::~AppConfig() = default;

void AppConfig::Init(const std::filesystem::path& app_dir) {
    app_dir_ = app_dir;
    verge_yaml_path_ = app_dir_ / "verge.yaml";
    window_state_json_path_ = app_dir_ / "window_state.json";

    std::filesystem::create_directories(app_dir_);
    LoadVergeConfig();
}

bool AppConfig::LoadVergeConfig() {
    if (!std::filesystem::exists(verge_yaml_path_)) {
        return SaveVergeConfig();
    }

    std::ifstream ifs(verge_yaml_path_);
    if (!ifs.is_open()) return false;

    std::string line;
    while (std::getline(ifs, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = Trim(line.substr(0, colon));
        std::string val = Trim(line.substr(colon + 1));

        if (key == "config_version") config_version_ = std::stoul(val);
        else if (key == "theme_mode") theme_mode_ = val;
        else if (key == "skin") skin_ = val;
        else if (key == "slider1_val") slider1_val_ = std::stof(val);
        else if (key == "slider2_val") slider2_val_ = std::stof(val);
        else if (key == "rule_fallback") rule_fallback_ = val;
        else if (key == "enable_system_proxy") enable_system_proxy_ = (val == "true");
        else if (key == "enable_tun_mode") enable_tun_mode_ = (val == "true");
        else if (key == "enable_ipv6") enable_ipv6_ = (val == "true");
        else if (key == "enable_auto_launch") enable_auto_launch_ = (val == "true");
        else if (key == "enable_silent_start") enable_silent_start_ = (val == "true");
        else if (key == "auto_close_connection") auto_close_connection_ = (val == "true");
        else if (key == "verge_mixed_port") verge_mixed_port_ = static_cast<uint16_t>(std::stoul(val));
        else if (key == "proxy_host") proxy_host_ = val;
    }
    return true;
}

bool AppConfig::SaveVergeConfig() {
    std::ofstream ofs(verge_yaml_path_);
    if (!ofs.is_open()) return false;

    ofs << "# ClashWin Application Persistent Configuration\n";
    ofs << "config_version: " << config_version_ << "\n";
    ofs << "theme_mode: \"" << theme_mode_ << "\"\n";
    ofs << "skin: \"" << skin_ << "\"\n";
    ofs << "slider1_val: " << slider1_val_ << "\n";
    ofs << "slider2_val: " << slider2_val_ << "\n";
    ofs << "rule_fallback: \"" << rule_fallback_ << "\"\n";
    ofs << "enable_system_proxy: " << (enable_system_proxy_ ? "true" : "false") << "\n";
    ofs << "enable_tun_mode: " << (enable_tun_mode_ ? "true" : "false") << "\n";
    ofs << "enable_ipv6: " << (enable_ipv6_ ? "true" : "false") << "\n";
    ofs << "enable_auto_launch: " << (enable_auto_launch_ ? "true" : "false") << "\n";
    ofs << "enable_silent_start: " << (enable_silent_start_ ? "true" : "false") << "\n";
    ofs << "auto_close_connection: " << (auto_close_connection_ ? "true" : "false") << "\n";
    ofs << "verge_mixed_port: " << verge_mixed_port_ << "\n";
    ofs << "proxy_host: \"" << proxy_host_ << "\"\n";

    return true;
}

bool AppConfig::LoadWindowState(WindowGeometry& out_geom) {
    if (!std::filesystem::exists(window_state_json_path_)) {
        return false;
    }

    std::ifstream ifs(window_state_json_path_);
    if (!ifs.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    auto extract_num = [&](std::string_view key) -> std::optional<double> {
        auto pos = content.find(key);
        if (pos == std::string::npos) return std::nullopt;
        auto colon = content.find(':', pos);
        if (colon == std::string::npos) return std::nullopt;
        size_t start = content.find_first_of("0123456789.-", colon);
        if (start == std::string::npos) return std::nullopt;
        size_t end = content.find_first_not_of("0123456789.-", start);
        std::string num_str = (end == std::string::npos) ? content.substr(start) : content.substr(start, end - start);
        try {
            return std::stod(num_str);
        } catch (...) {
            return std::nullopt;
        }
    };

    auto w = extract_num("\"width\"");
    auto h = extract_num("\"height\"");
    auto x = extract_num("\"x\"");
    auto y = extract_num("\"y\"");

    if (!w.has_value() || !h.has_value()) return false;

    // 守护与钳制：防止脏数据或者极端缩放
    out_geom.width = std::clamp(*w, geometry::kMinimalWidth, geometry::kMaxWidth);
    out_geom.height = std::clamp(*h, geometry::kMinimalHeight, geometry::kMaxHeight);
    out_geom.x = x;
    out_geom.y = y;

    return true;
}

void AppConfig::WriteGeometrySync(uint64_t seq, const WindowGeometry& geom) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (seq <= written_seq_.load()) {
        return; // 丢弃乱序旧值
    }

    std::ofstream ofs(window_state_json_path_);
    if (ofs.is_open()) {
        ofs << "{\n";
        ofs << "  \"width\": " << geom.width << ",\n";
        ofs << "  \"height\": " << geom.height;
        if (geom.x.has_value()) ofs << ",\n  \"x\": " << *geom.x;
        if (geom.y.has_value()) ofs << ",\n  \"y\": " << *geom.y;
        ofs << "\n}\n";
    }

    written_seq_.store(seq);
}

void AppConfig::FlushPendingGeometry() {
    WindowGeometry g;
    uint64_t s = 0;
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        g = pending_geom_;
        s = geometry_seq_.load();
    }
    last_save_time_ms_.store(CurrentTimeMs());
    WriteGeometrySync(s, g);
}

void AppConfig::SaveWindowStateThrottled(double width, double height, std::optional<double> x, std::optional<double> y) {
    // 过滤 0x0 摧毁脏事件 (带 1px 容差)
    if (width < (geometry::kMinimalWidth - 1.0) || height < (geometry::kMinimalHeight - 1.0)) {
        return;
    }

    geometry_seq_.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        pending_geom_.width = width;
        pending_geom_.height = height;
        pending_geom_.x = x;
        pending_geom_.y = y;
    }

    int64_t now = CurrentTimeMs();
    int64_t elapsed = now - last_save_time_ms_.load();

    if (elapsed >= geometry::kGeometrySaveIntervalMs) {
        FlushPendingGeometry();
        return;
    }

    // 尾随补写防丢 (Trailing Write)
    if (!trailing_write_armed_.exchange(true)) {
        int64_t wait_ms = std::max<int64_t>(10, geometry::kGeometrySaveIntervalMs - elapsed);
        std::thread([this, wait_ms]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
            trailing_write_armed_.store(false);
            FlushPendingGeometry();
        }).detach();
    }
}

void AppConfig::SaveWindowStateImmediately(double width, double height, std::optional<double> x, std::optional<double> y) {
    if (width < (geometry::kMinimalWidth - 1.0) || height < (geometry::kMinimalHeight - 1.0)) {
        return;
    }
    uint64_t seq = geometry_seq_.fetch_add(1) + 1;
    WindowGeometry geom{width, height, x, y};
    WriteGeometrySync(seq, geom);
}

} // namespace clashwin::config

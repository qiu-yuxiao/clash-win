#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

void TestDistributionArtifacts() {
    std::cout << "[Test 1] Testing Distribution Artifacts Integrity..." << std::endl;

    std::filesystem::path dist_dir = std::filesystem::current_path() / "build" / "dist" / "ClashWin";
    assert(std::filesystem::exists(dist_dir));

    // 1. 主二进制
    std::filesystem::path main_exe = dist_dir / "ClashWin.exe";
    assert(std::filesystem::exists(main_exe));
    assert(std::filesystem::file_size(main_exe) > 100 * 1024); // > 100KB

    // 2. 侧车目录与关键伴生程序
    std::filesystem::path sidecar_dir = dist_dir / "sidecar";
    assert(std::filesystem::exists(sidecar_dir));

    std::filesystem::path mihomo_exe = sidecar_dir / "mini-mihomo.exe";
    assert(std::filesystem::exists(mihomo_exe));
    assert(std::filesystem::file_size(mihomo_exe) > 10 * 1024 * 1024); // > 10MB

    std::filesystem::path svc_exe = sidecar_dir / "clash-verge-service.exe";
    assert(std::filesystem::exists(svc_exe));

    std::filesystem::path svc_install = sidecar_dir / "clash-verge-service-install.exe";
    assert(std::filesystem::exists(svc_install));

    std::filesystem::path svc_uninstall = sidecar_dir / "clash-verge-service-uninstall.exe";
    assert(std::filesystem::exists(svc_uninstall));

    std::filesystem::path loopback_exe = sidecar_dir / "enableLoopback.exe";
    assert(std::filesystem::exists(loopback_exe));

    // 3. 服务辅助脚本
    assert(std::filesystem::exists(dist_dir / "install-service.bat"));
    assert(std::filesystem::exists(dist_dir / "uninstall-service.bat"));

    // 4. 便携式 ZIP 压缩包
    std::filesystem::path zip_path = std::filesystem::current_path() / "build" / "bin" / "ClashWin-Portable-x64.zip";
    assert(std::filesystem::exists(zip_path));
    assert(std::filesystem::file_size(zip_path) > 5 * 1024 * 1024); // > 5MB

    std::cout << "  -> Distribution artifacts & sidecars verified successfully!" << std::endl;
}

void TestNsisScriptValidity() {
    std::cout << "[Test 2] Testing NSIS Installer Script Specification..." << std::endl;

    std::filesystem::path nsi_path = std::filesystem::current_path() / "installer" / "ClashWin-Setup.nsi";
    assert(std::filesystem::exists(nsi_path));

    std::ifstream ifs(nsi_path);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    assert(content.find("RequestExecutionLevel admin") != std::string::npos);
    assert(content.find("clash_verge_service") != std::string::npos);
    assert(content.find("clash-verge-service-install.exe") != std::string::npos);
    assert(content.find("clash-verge-service-uninstall.exe") != std::string::npos);
    assert(content.find("mini-mihomo.exe") != std::string::npos);
    assert(content.find("Uninstall.exe") != std::string::npos);
    assert(content.find("EstimatedSize") != std::string::npos);

    std::cout << "  -> NSIS Installer Script specification verified!" << std::endl;
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "  ClashWin Phase 4 Slice 11 - Installer Test      " << std::endl;
    std::cout << "==================================================" << std::endl;

    TestDistributionArtifacts();
    TestNsisScriptValidity();

    std::cout << "\n>>> [ALL SLICE 11 TESTS PASSED SUCCESSFULLY!] <<<\n" << std::endl;
    return 0;
}

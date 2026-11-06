#pragma once
// T25 网络与主机信息提供者与 Mock 注入抽象
// 统一封装 get_system_hostname / hostname, get_network_interfaces, get_network_interfaces_info
#include <memory>
#include <string>
#include <vector>

namespace clashwin::system {

struct NetworkInterfaceInfo {
  std::string name;
  std::string mac;
  std::vector<std::string> addresses;
};

class INetworkInfoProvider {
 public:
  virtual ~INetworkInfoProvider() = default;
  virtual std::string get_hostname() = 0;
  virtual std::vector<std::string> list_interfaces() = 0;
  virtual std::vector<NetworkInterfaceInfo> list_interfaces_info() = 0;
};

class DefaultNetworkInfoProvider : public INetworkInfoProvider {
 public:
  std::string get_hostname() override;
  std::vector<std::string> list_interfaces() override;
  std::vector<NetworkInterfaceInfo> list_interfaces_info() override;
};

std::shared_ptr<INetworkInfoProvider> get_network_info_provider();
void set_network_info_provider(std::shared_ptr<INetworkInfoProvider> provider);

// 【P0-6】探测承载出网的物理网卡名（FriendlyName，如 "WLAN 2"）。
// 用于把内核 interface-name 钉在物理网卡上，避免同机其它 TUN（Clash Mini 的
// Meta Tunnel）抢走默认路由并劫持 DNS 后，本内核解析到 198.18.x.x 假 IP → 出网
// 502。排除 loopback / IF_TYPE_TUNNEL 及 Mihomo/Clash/Meta 命名的虚拟网卡。
// 无可用网卡时返回空串。
std::string detect_outbound_interface();

}  // namespace clashwin::system

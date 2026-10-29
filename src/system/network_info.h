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

}  // namespace clashwin::system

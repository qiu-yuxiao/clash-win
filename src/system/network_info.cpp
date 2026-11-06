#include "system/network_info.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <iptypes.h>

#include <mutex>
#include "util/utf.h"

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace clashwin::system {

namespace {

std::mutex g_provider_mu;
std::shared_ptr<INetworkInfoProvider> g_provider;

}  // namespace

std::string DefaultNetworkInfoProvider::get_hostname() {
  wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1] = {};
  DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
  if (!GetComputerNameW(buf, &size)) {
    return "localhost";
  }
  return utf::utf16_to_utf8(buf);
}

std::vector<std::string> DefaultNetworkInfoProvider::list_interfaces() {
  std::vector<std::string> names;
  ULONG size = 16 * 1024;
  std::vector<char> buf(size);
  auto* aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
  if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
                           nullptr, aa, &size) == ERROR_BUFFER_OVERFLOW) {
    buf.resize(size);
    aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
  }
  if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
                           nullptr, aa, &size) != NO_ERROR) {
    return names;
  }
  for (auto* a = aa; a; a = a->Next) {
    if (a->OperStatus != IfOperStatusUp) continue;
    char name[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1, name, sizeof(name), nullptr, nullptr);
    names.push_back(std::string(name));
  }
  return names;
}

std::vector<NetworkInterfaceInfo> DefaultNetworkInfoProvider::list_interfaces_info() {
  std::vector<NetworkInterfaceInfo> result;
  ULONG size = 16 * 1024;
  std::vector<char> buf(size);
  auto* aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
  if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
                           nullptr, aa, &size) == ERROR_BUFFER_OVERFLOW) {
    buf.resize(size);
    aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
  }
  if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
                           nullptr, aa, &size) != NO_ERROR) {
    return result;
  }
  for (auto* a = aa; a; a = a->Next) {
    if (a->OperStatus != IfOperStatusUp) continue;
    char name[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, a->FriendlyName, -1, name, sizeof(name), nullptr, nullptr);

    NetworkInterfaceInfo info;
    info.name = name;

    char mac[32] = {};
    for (ULONG i = 0; i < a->PhysicalAddressLength && i < 8; ++i) {
      char one[4];
      sprintf_s(one, "%02X", a->PhysicalAddress[i]);
      strcat_s(mac, one);
      if (i + 1 < a->PhysicalAddressLength) strcat_s(mac, ":");
    }
    info.mac = mac;

    for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
      char ip[64] = {};
      auto fam = u->Address.lpSockaddr->sa_family;
      if (fam == AF_INET) {
        InetNtopA(AF_INET,
                  &reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr, ip,
                  sizeof(ip));
      } else if (fam == AF_INET6) {
        InetNtopA(AF_INET6,
                  &reinterpret_cast<sockaddr_in6*>(u->Address.lpSockaddr)->sin6_addr, ip,
                  sizeof(ip));
      } else {
        continue;
      }
      info.addresses.push_back(ip);
    }
    result.push_back(std::move(info));
  }
  return result;
}

std::shared_ptr<INetworkInfoProvider> get_network_info_provider() {
  std::lock_guard<std::mutex> lock(g_provider_mu);
  if (!g_provider) {
    g_provider = std::make_shared<DefaultNetworkInfoProvider>();
  }
  return g_provider;
}

void set_network_info_provider(std::shared_ptr<INetworkInfoProvider> provider) {
  std::lock_guard<std::mutex> lock(g_provider_mu);
  g_provider = std::move(provider);
}

std::string detect_outbound_interface() {
  ULONG size = 16 * 1024;
  std::vector<char> buf(size);
  auto* aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
  DWORD flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
  if (GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, aa, &size) ==
      ERROR_BUFFER_OVERFLOW) {
    buf.resize(size);
    aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
  }
  if (GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, aa, &size) != NO_ERROR) {
    return {};
  }

  std::string best;
  int best_score = -1;
  for (auto* a = aa; a; a = a->Next) {
    if (a->OperStatus != IfOperStatusUp) continue;
    if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK || a->IfType == IF_TYPE_TUNNEL) continue;
    std::string name = utf::utf16_to_utf8(a->FriendlyName);
    if (name.empty()) continue;
    // 明确排除常见 Clash/TUN 虚拟网卡（部分版本会把 TUN 报成 Ethernet）
    if (name.rfind("Mihomo", 0) == 0 || name.rfind("Clash", 0) == 0 ||
        name.rfind("Meta", 0) == 0) {
      continue;
    }
    bool has_v4 = false;
    for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
      if (u->Address.lpSockaddr && u->Address.lpSockaddr->sa_family == AF_INET) {
        has_v4 = true;
        break;
      }
    }
    if (!has_v4) continue;
    bool has_gw = a->FirstGatewayAddress != nullptr;
    bool wireless = a->IfType == IF_TYPE_IEEE80211;
    int score = (has_gw ? 4 : 0) + 2 + (wireless ? 1 : 0);
    if (score > best_score) {
      best_score = score;
      best = name;
    }
  }
  return best;
}

}  // namespace clashwin::system

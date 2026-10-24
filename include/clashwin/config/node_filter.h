#pragma once

#include <string>
#include <string_view>

namespace clashwin::config {

class NodeFilter {
public:
    // 判断是否为虚拟节点、广告或废弃通知节点
    static bool IsDummyNode(std::string_view name);

    // 判断节点服务器地址是否合法可用
    static bool IsValidNodeServer(std::string_view server);
};

} // namespace clashwin::config

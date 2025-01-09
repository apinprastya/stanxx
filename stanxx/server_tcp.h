#pragma once

#include <seastar/core/future.hh>

namespace stanxx {
class ServerTcp {
    public:
    seastar::future<> listen (const std::string& address, int port);
};
} // namespace stanxx
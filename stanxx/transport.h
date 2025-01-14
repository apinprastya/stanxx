#pragma once

#include <seastar/core/future.hh>
#include <string>

namespace stanxx {

class Connection {
    public:
    virtual std::string id () const noexcept                               = 0;
    virtual int cpuId () const noexcept                                    = 0;
    virtual seastar::future<seastar::temporary_buffer<char>> read ()       = 0;
    virtual seastar::future<> write (seastar::temporary_buffer<char> data) = 0;
};

class Transport {
    public:
    virtual seastar::future<> listen (const std::string& address, int port) = 0;
    virtual seastar::future<> close ()                                      = 0;
    virtual seastar::future<> stop ()                                       = 0;
};

} // namespace stanxx
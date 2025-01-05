#pragma once

#include <memory>
#include <seastar/core/future.hh>
#include <string>

namespace stanxx {

class ClientTransport {
    public:
    virtual seastar::future<> connect (const std::string& address, int port) = 0;
    virtual seastar::future<> close () = 0;
    virtual seastar::future<> read ()  = 0;
    virtual seastar::future<> write () = 0;
};

class ServerTransport {
    public:
    virtual seastar::future<std::shared_ptr<ServerTransport>>
    listen (const std::string& address, int port) = 0;
    virtual seastar::future<> close ()            = 0;
    virtual seastar::future<> accept ()           = 0;
};

} // namespace stanxx
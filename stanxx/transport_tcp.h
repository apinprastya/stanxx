#pragma once

#include "transport.h"
#include <memory>
#include <seastar/net/api.hh>

namespace stanxx {

class TransportTcp : public ServerTransport,
                     public std::enable_shared_from_this<TransportTcp> {
    public:
    seastar::future<std::shared_ptr<ServerTransport>>
    listen (const std::string& address, int port) override;
    seastar::future<> close () override;
    seastar::future<> accept () override;

    private:
    seastar::server_socket listener;
};

} // namespace stanxx
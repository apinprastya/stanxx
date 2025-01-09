#pragma once

#include "transport_tcp.h"

#include <memory>
#include <seastar/core/app-template.hh>
#include <seastar/core/future.hh>
#include <seastar/core/sharded.hh>
#include <seastar/net/api.hh>

namespace stanxx {

class Server {
    public:
    Server ();
    ~Server ();
    void run (int argc, char** argv);

    private:
    std::shared_ptr<TransportTcp> tcpTransport;
    seastar::app_template app;
    seastar::sharded<TransportTcp> mainTransport;

    seastar::future<> handle_client (seastar::connected_socket conn);
};
} // namespace stanxx
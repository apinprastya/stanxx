#include "server.h"
#include "transport_tcp.h"
#include <csignal>
#include <memory>
#include <seastar/core/do_with.hh>
#include <seastar/core/future.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/signal.hh>
#include <seastar/net/api.hh>
#include <spdlog/spdlog.h>

namespace stanxx {

Server::Server () {
}

Server::~Server () {
}

void Server::run (int argc, char** argv) {
    tcpTransport = std::make_shared<TransportTcp> ();
    app.run (argc, argv, [this] {
        seastar::handle_signal (SIGINT, [this] () {
            spdlog::info ("SIGNINT");
            (void)tcpTransport->close ();
        });
        /*seastar::handle_signal (SIGTERM, [] () { seastar::engine_exit (); });
        seastar::handle_signal (SIGKILL, [] () { seastar::engine_exit (); });*/
        return seastar::do_with (tcpTransport,
        [this] (auto tcpServer) { return tcpServer->listen ("0.0.0.0", 4222); });
    });
    std::cout << "Server started" << std::endl;
}


seastar::future<> Server::handle_client (seastar::connected_socket conn) {
    std::cout << "New connection" << std::endl;
    return seastar::make_ready_future<> ();
}

} // namespace stanxx

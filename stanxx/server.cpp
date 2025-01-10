#include "server.h"
#include "transport_tcp.h"
#include <chrono>
#include <csignal>
#include <memory>
#include <seastar/core/do_with.hh>
#include <seastar/core/future.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/signal.hh>
#include <seastar/core/sleep.hh>
#include <seastar/net/api.hh>
#include <spdlog/spdlog.h>

namespace stanxx {

Server::Server () {
}

Server::~Server () {
}

void Server::run (int argc, char** argv) {
    app.run (argc, argv, [this] {
        return seastar::do_with (std::make_shared<TransportTcp> (),
        [this] (auto tcpServer) {
            seastar::handle_signal (
            SIGINT,
            [tcpServer] () {
                spdlog::info ("SIGNINT");
                (void)tcpServer->close ();
            },
            true);
            return tcpServer->listen ("0.0.0.0", 4222);
        })
        .finally ([this] () {
            spdlog::info ("server listen ended");
            seastar::engine ().exit (0);
        });
    });
}


seastar::future<> Server::handle_client (seastar::connected_socket conn) {
    std::cout << "New connection" << std::endl;
    return seastar::make_ready_future<> ();
}

} // namespace stanxx

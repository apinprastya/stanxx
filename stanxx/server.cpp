#include "server.h"
#include "subscriber.h"
#include "transport_tcp.h"
#include <csignal>
#include <memory>
#include <seastar/core/do_with.hh>
#include <seastar/core/future.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/signal.hh>
#include <seastar/core/sleep.hh>
#include <seastar/net/api.hh>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>

namespace stanxx {

Server::Server () {
    _subscribeManager = std::make_unique<SubscriberManager> ();
}

Server::~Server () {
}

void Server::run (int argc, char** argv) {
    spdlog::set_level (spdlog::level::err);
    app.run (argc, argv, [this] {
        return seastar::do_with (std::make_shared<TransportTcp> (this),
        [this] (auto tcpServer) {
            seastar::handle_signal (
            SIGINT,
            [tcpServer] () {
                spdlog::info ("SIGNINT");
                (void)tcpServer->stop ();
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

} // namespace stanxx

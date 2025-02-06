#include "server.h"
#include "subscriber.h"
#include "transport_tcp.h"
#include <asio/co_spawn.hpp>
#include <csignal>
#include <memory>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>

namespace stanxx {

Server::Server () {
    _subscribeManager = std::make_unique<SubscriberManager> ();
}

Server::~Server () {
}

void Server::run (int argc, char** argv) {
    spdlog::set_level (spdlog::level::info);
    TransportTcp tcpServer (this, &_ioContext);
    asio::co_spawn (_ioContext, tcpServer.listen ("0.0.0.0", 4222), asio::detached);
    _ioContext.run ();
    /*app.run (argc, argv, [this] {
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
    });*/
}

} // namespace stanxx

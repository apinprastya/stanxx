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
    spdlog::set_level (spdlog::level::err);
    TransportTcp tcpServer (this, &_ioContext);
    asio::co_spawn (_ioContext, tcpServer.listen ("0.0.0.0", 4222), asio::detached);
    _ioContext.run ();
}

} // namespace stanxx

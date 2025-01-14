#include "server.h"
#include "subscriber.h"
#include "transport_tcp.h"
#include <csignal>
#include <memory>
#include <seastar/core/coroutine.hh>
#include <seastar/core/do_with.hh>
#include <seastar/core/future.hh>
#include <seastar/core/metrics_api.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/signal.hh>
#include <seastar/core/sleep.hh>
#include <seastar/http/httpd.hh>
#include <seastar/net/api.hh>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>

namespace stanxx {

Server::Server () {
    _clusteredSubscribeManager = std::make_unique<ClusteredSubscriberManager> ();
}

Server::~Server () {
}

void Server::run (int argc, char** argv) {
    spdlog::set_level (spdlog::level::err);
    app.run (argc, argv, [this] -> seastar::future<> {
        co_await mainTransport.start (_clusteredSubscribeManager.get ());
        seastar::handle_signal (
        SIGINT,
        [this] () -> seastar::future<> {
            spdlog::info ("SIGNINT");
            return mainTransport.invoke_on_all (
            [] (TransportTcp& t) { return t.close (); });
        },
        true);
        co_await _clusteredSubscribeManager->start ();
        co_await mainTransport
        .invoke_on_all (
        [] (TransportTcp& t) { return t.listen ("0.0.0.0", 4222); })
        .then ([this] {
            spdlog::info ("server listen ended");
            return mainTransport.stop ();
        });
        /*return mainTransport.start (_clusteredSubscribeManager.get ()).then ([this] {
            seastar::handle_signal (
            SIGINT,
            [this] () -> seastar::future<> {
                spdlog::info ("SIGNINT");
                return mainTransport.invoke_on_all (
                [] (TransportTcp& t) { return t.close (); });
            },
            true);

            _clusteredSubscribeManager->start ();

            return mainTransport
            .invoke_on_all (
            [] (TransportTcp& t) { return t.listen ("0.0.0.0", 4222); })
            .then ([this] {
                spdlog::info ("server listen ended");
                return mainTransport.stop ();
            });
        });*/
    });
    spdlog::info ("server ended");
}

} // namespace stanxx

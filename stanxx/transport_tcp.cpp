#include "transport_tcp.h"
#include <coroutine>
#include <seastar/core/do_with.hh>
#include <seastar/core/future.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/seastar.hh>
#include <seastar/net/socket_defs.hh>

namespace stanxx {

seastar::future<std::shared_ptr<ServerTransport>>
TransportTcp::listen (const std::string& address, int port) {
    seastar::socket_address sa (seastar::ipv4_addr (address, port));
    listener = seastar::engine ().listen (sa);
    return listener.accept ().then ([this] (seastar::accept_result ar) {
        return seastar::make_ready_future<std::shared_ptr<ServerTransport>> (
        shared_from_this ());
    });

    /*return seastar::make_ready_future<std::shared_ptr<ServerTransport>> (
    shared_from_this ());*/
}

seastar::future<> TransportTcp::close () {
    return seastar::make_ready_future<> ();
}

seastar::future<> TransportTcp::accept () {
    return seastar::make_ready_future<> ();
}

} // namespace stanxx
#include "transport_tcp.h"
#include "client.h"
#include "server.h"
#include <asio/as_tuple.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <chrono>
#include <spdlog/spdlog.h>

static boost::uuids::random_generator generator;

namespace stanxx {

Connection::Connection (TransportTcp* server, asio::ip::tcp::socket&& socket)
: _server (server), _socket (std::move (socket)) {
}

Connection::~Connection () {
    spdlog::debug ("connection destroyed");
}

void Connection::queue (std::vector<char>&& data) {
    _writeQueue.push (std::move (data));
}

asio::awaitable<void> Connection::runWritePending () {
    spdlog::debug ("run write pending");
    while (true) {
        if (_writeQueue.empty ()) {
            asio::steady_timer timer (
            co_await asio::this_coro::executor, std::chrono::milliseconds (1));
            spdlog::debug ("write queue empty");
            co_await timer.async_wait (asio::use_awaitable);
            spdlog::debug ("write queue empty 123");
            continue;
        }
        auto data = std::move (_writeQueue.front ());
        _writeQueue.pop ();
        co_await _socket.async_send (asio::buffer (data), asio::use_awaitable);
    }
}

TransportTcp::TransportTcp (Server* server, asio::io_context* ioContext)
: _server (server), _ioContext (ioContext) {
}

TransportTcp::~TransportTcp () {
}


asio::awaitable<void> TransportTcp::handleConnection (asio::ip::tcp::socket&& socket) {
    spdlog::debug ("new connection");
    Connection connection (this, std::move (socket));
    Client client ("", 0, &connection, _server);
    asio::co_spawn (connection._socket.get_executor (),
    connection.runWritePending (), asio::detached);
    // co_await socket.async_wait (socket.wait_write, asio::use_awaitable);
    static char* welcomeData =
    "INFO "
    "{\"server_id\":"
    "\"NCEUKVMQR4KCNGMKEAIEFS5OF4VMI34DXCTZ5HBFR4YSLETPHFDEWIRQ\",\"server_"
    "name\":\"NCEUKVMQR4KCNGMKEAIEFS5OF4VMI34DXCTZ5HBFR4YSLETPHFDEWIRQ\","
    "\"version\":\"2.11.0-dev\",\"proto\" : 1,\"go\" : "
    "\"go1.23.3\",\"host\" : \"0.0.0.0\",\"port\" : 4222,\"headers\" : "
    "true,\"max_payload\" : "
    "1048576,\"client_id\":5,\"client_ip\":\"127.0.0.1\",\"xkey\":"
    "\"XBRNVBBFW45EB3RA7JI3D6HU6ROXESE2EU2IXXTWYOCENKIGI5AW2GU2\"}\r\n";
    auto [ec, xx] = co_await connection._socket.async_send (
    asio::buffer (welcomeData, std::strlen (welcomeData)),
    asio::as_tuple (asio::use_awaitable));
    if (ec) {
        spdlog::info ("send info error: {}", ec.message ());
        co_return;
    }
    char data[512];
    while (true) {
        auto [ec, length] = co_await connection._socket.async_read_some (
        asio::buffer (data), asio::as_tuple (asio::use_awaitable));
        if (ec) {
            if (ec != asio::error::eof)
                spdlog::error ("read error: {}", ec.message ());
            break;
        }
        client.read (std::span<char> (data, length));
    }
    spdlog::debug ("end handle connection");
}

asio::awaitable<void> TransportTcp::listen (const std::string& address, int port) {
    spdlog::info ("listening on {}:{}", address, port);
    asio::ip::tcp::acceptor acceptor (
    *_ioContext, asio::ip::tcp::endpoint (asio::ip::tcp::v4 (), port));
    while (true) {
        auto [ec, socket] =
        co_await acceptor.async_accept (asio::as_tuple (asio::use_awaitable));
        if (ec) {
            spdlog::error ("Accept error: {}", ec.message ());
            continue;
        }
        socket.non_blocking (true);
        socket.set_option (asio::ip::tcp::no_delay (true));
        asio::co_spawn (acceptor.get_executor (),
        handleConnection (std::move (socket)), asio::detached);
    }


    /*return seastar::repeat ([this] () {
        spdlog::debug ("waiting for connection");
        return _listener.accept ()
        .then ([this] (seastar::accept_result ar) {
            spdlog::debug ("new connection accepted");
            (void)seastar::do_with (
            std::make_unique<Connection> (this, std::move (ar.connection)),
            [this] (auto& conn) {
                return conn->process ([&conn, this] (Connection* connection)
    mutable { return seastar::do_with ( std::make_unique<Client> (connection->id
    (), connection->cpuId (), connection, _server),
                    [] (auto& client) { return client->run (); });
                });
            });
            return seastar::make_ready_future<seastar::stop_iteration> (
            seastar::stop_iteration::no);
        })
        .handle_exception ([this] (std::exception_ptr e) {
            return seastar::make_ready_future<seastar::stop_iteration> (
            seastar::stop_iteration::yes);
        });
    });*/
}

asio::awaitable<void> TransportTcp::stop () {
    spdlog::info ("closing tcp server");
    //_listener.abort_accept ();

    /*for (auto&& c : _connections) {
        c.shutdown_input ();
    }*/
    co_return;
}

} // namespace stanxx
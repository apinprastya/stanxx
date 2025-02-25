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
#include <iostream>
#include <spdlog/spdlog.h>

static boost::uuids::random_generator generator;

namespace stanxx {

Connection::Connection (TransportTcp* server, asio::ip::tcp::socket&& socket, asio::io_context* ioContext)
: _server (server), _socket (std::move (socket)), _ioContext (ioContext) {
}

Connection::~Connection () {
    SPDLOG_DEBUG ("connection destroyed");
}

void Connection::queue (std::vector<char>&& data) {
    _writeQueue.push (std::move (data));
}

asio::awaitable<void> Connection::runWritePending () {
    SPDLOG_DEBUG ("run write pending");
    std::vector<asio::const_buffer> batch_buffers;
    std::vector<std::vector<char>> pending_data;
    batch_buffers.reserve (64); // Pre-allocate for common batch size
    pending_data.reserve (64);
    while (_running) {
        if (_writeQueue.empty ()) {
            asio::steady_timer timer (*_ioContext, std::chrono::milliseconds (1));
            spdlog::info ("write queue empty");
            co_await timer.async_wait (asio::use_awaitable);
            continue;
        }
        /*auto data = std::move (_writeQueue.front ());
        _writeQueue.pop ();
        co_await _socket.async_send (asio::buffer (data), asio::use_awaitable);*/
        // spdlog::info ("pie iki {} {}", _writeQueue.size (), batch_buffers.size ());
        batch_buffers.clear ();
        pending_data.clear ();
        while (!_writeQueue.empty () && batch_buffers.size () < 64) {
            auto data = std::move (_writeQueue.front ());
            _writeQueue.pop ();
            pending_data.push_back (std::move (data));
            batch_buffers.push_back (asio::buffer (
            pending_data.back ().data (), pending_data.back ().size ()));
        }

        // Send batch
        if (!batch_buffers.empty ()) {
            try {
                co_await asio::async_write (_socket, batch_buffers, asio::use_awaitable);
            } catch (const std::exception& e) {
                spdlog::error ("Write error: {}", e.what ());
                break;
            }
        }
    }
}

void Connection::stop () {
    _running = false;
}

TransportTcp::TransportTcp (Server* server, asio::io_context* ioContext)
: _server (server), _ioContext (ioContext) {
}

TransportTcp::~TransportTcp () {
}


asio::awaitable<void> TransportTcp::handleConnection (asio::ip::tcp::socket&& socket) {
    SPDLOG_DEBUG ("new connection");

    static constexpr size_t BUFFER_SIZE = 64 * 1024; // 128KB
    alignas (64) char data[BUFFER_SIZE];             // Cache line aligned

    Connection connection (this, std::move (socket), _ioContext);
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
        // SPDLOG_INFO ("send info error: {}", [&ec] () { return ec.message (); });
        co_return;
    }
    // char data[65535];
    while (true) {
        // auto start        = std::chrono::high_resolution_clock::now ();
        auto [ec, length] = co_await connection._socket.async_read_some (
        asio::buffer (data), asio::as_tuple (asio::use_awaitable));
        if (ec) {
            if (ec != asio::error::eof)
                spdlog::error ("read error: {}", ec.message ());
            break;
        }
        /*auto end = std::chrono::high_resolution_clock::now ();
        auto duration =
        std::chrono::duration_cast<std::chrono::microseconds> (end - start);

        std::cout << "Read time: " << duration.count () << " microseconds\n";*/
        client.read (std::span<char> (data, length));
    }
    connection.stop ();
    spdlog::debug ("end handle connection");
}

asio::awaitable<void> TransportTcp::listen (const std::string& address, int port) {
    spdlog::info ("listening on {}:{}", address, port);
    asio::ip::tcp::acceptor acceptor (
    *_ioContext, asio::ip::tcp::endpoint (asio::ip::tcp::v4 (), port));
    acceptor.set_option (asio::ip::tcp::acceptor::reuse_address (true));
    acceptor.set_option (asio::socket_base::send_buffer_size (256 * 1024));
    acceptor.set_option (asio::socket_base::receive_buffer_size (256 * 1024));
    while (true) {
        auto [ec, socket] =
        co_await acceptor.async_accept (asio::as_tuple (asio::use_awaitable));
        if (ec) {
            spdlog::error ("Accept error: {}", ec.message ());
            continue;
        }
        socket.non_blocking (true);
        socket.set_option (asio::ip::tcp::no_delay (true));
        socket.set_option (asio::socket_base::keep_alive (true));
        asio::co_spawn (acceptor.get_executor (),
        handleConnection (std::move (socket)), asio::detached);
    }
}

asio::awaitable<void> TransportTcp::stop () {
    spdlog::info ("closing tcp server");
    co_return;
}

} // namespace stanxx
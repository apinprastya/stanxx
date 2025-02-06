#pragma once

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <boost/intrusive/list_hook.hpp>
#include <memory>
#include <queue>
#include <seastar/core/gate.hh>
#include <seastar/core/iostream.hh>
#include <seastar/core/queue.hh>
#include <seastar/core/resource.hh>
#include <seastar/core/temporary_buffer.hh>
#include <seastar/net/api.hh>
#include <spdlog/spdlog.h>
#include <vector>

#define QUEUE_SIZE 512

namespace stanxx {

class Server;
class TransportTcp;
class Connection;

using handler_t = std::function<asio::awaitable<void> (Connection* connection)>;

class Connection : public boost::intrusive::list_base_hook<> {
    public:
    Connection (TransportTcp* server, asio::ip::tcp::socket&& socket);
    ~Connection ();
    void queue (std::vector<char>&& data);

    asio::awaitable<void> runWritePending ();

    private:
    TransportTcp* _server;
    asio::ip::tcp::socket _socket;
    std::queue<std::vector<char>> _writeQueue;

    friend class TransportTcp;
};

class TransportTcp {
    public:
    TransportTcp (Server* server, asio::io_context* ioContext);
    ~TransportTcp ();
    asio::awaitable<void> listen (const std::string& address, int port);
    asio::awaitable<void> stop ();

    friend class Connection;

    private:
    Server* _server;
    asio::io_context* _ioContext;
    boost::intrusive::list<Connection> _connections;

    asio::awaitable<void> handleConnection (asio::ip::tcp::socket&& socket);
};

} // namespace stanxx
#pragma once

#include <boost/intrusive/list_hook.hpp>
#include <seastar/core/gate.hh>
#include <seastar/core/iostream.hh>
#include <seastar/core/queue.hh>
#include <seastar/core/resource.hh>
#include <seastar/core/temporary_buffer.hh>
#include <seastar/net/api.hh>
#include <spdlog/spdlog.h>

#define QUEUE_SIZE 512

namespace stanxx {

class Server;
class TransportTcp;
class Connection;

using handler_t = std::function<seastar::future<> (Connection* connection)>;

class Connection : public boost::intrusive::list_base_hook<> {
    public:
    Connection (TransportTcp* server, seastar::connected_socket&& fd);
    ~Connection ();

    seastar::future<> process (handler_t handler);
    void shutdown_input ();
    seastar::future<> close ();
    seastar::future<seastar::temporary_buffer<char>> read ();
    seastar::future<> write (seastar::temporary_buffer<char> data);
    seastar::future<> flush ();
    void writeAsync (seastar::temporary_buffer<char> data);
    inline std::string id () const noexcept {
        return _id;
    }
    inline int cpuId () const noexcept {
        return _cpuId;
    }

    protected:
    seastar::future<> read_loop ();
    seastar::future<seastar::stop_iteration> read_one ();
    seastar::future<> write_loop ();
    void on_new_connection ();

    private:
    TransportTcp* _server;
    std::string _id{};
    int _cpuId{};
    seastar::input_stream<char> _read_buf;
    seastar::output_stream<char> _write_buf;
    seastar::queue<seastar::temporary_buffer<char>> _input_buffer{ 512 };
    seastar::queue<seastar::temporary_buffer<char>> _output_buffer{ 512 };
};

class TransportTcp {
    public:
    TransportTcp (Server* server);
    ~TransportTcp ();
    seastar::future<> listen (const std::string& address, int port);
    seastar::future<> stop ();

    friend class Connection;

    private:
    int _cpuId;
    Server* _server;
    boost::intrusive::list<Connection> _connections;
    seastar::server_socket listener;
    seastar::gate gate;
};

} // namespace stanxx
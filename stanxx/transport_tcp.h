#pragma once

#include "transport.h"
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

class ClusteredSubscriberManager;
class TransportTcp;
class ConnectionTcp;

class ConnectionTcp : public Connection, public boost::intrusive::list_base_hook<> {
    public:
    ConnectionTcp (TransportTcp* server, seastar::connected_socket&& fd);
    ~ConnectionTcp ();

    seastar::future<> run ();
    void shutdown_input ();
    seastar::future<> close ();
    seastar::future<seastar::temporary_buffer<char>> read () override;
    seastar::future<> write (seastar::temporary_buffer<char> data) override;
    inline std::string id () const noexcept override {
        return _id;
    }
    inline int cpuId () const noexcept override {
        return _cpuId;
    }

    protected:
    seastar::future<> read_loop ();
    seastar::future<> write_loop ();

    private:
    TransportTcp* _server{};
    std::string _id{};
    int _cpuId{};
    seastar::input_stream<char> _read_buf;
    seastar::output_stream<char> _write_buf;
    seastar::queue<seastar::temporary_buffer<char>> _input_buffer{ 512 };
    seastar::queue<seastar::temporary_buffer<char>> _output_buffer{ 512 };
};

class TransportTcp : public Transport {
    public:
    TransportTcp (ClusteredSubscriberManager* _subscriberManager);
    ~TransportTcp ();
    seastar::future<> listen (const std::string& address, int port) override;
    seastar::future<> close () override;
    seastar::future<> stop () override;

    friend class ConnectionTcp;

    private:
    int _cpuId;
    ClusteredSubscriberManager* _subscriberManager;
    boost::intrusive::list<ConnectionTcp> _connections;
    seastar::server_socket _listener;
    seastar::gate _gate;

    seastar::future<> handleConnection (seastar::accept_result ar);
};

} // namespace stanxx
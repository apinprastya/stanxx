#include "transport_tcp.h"
#include "client.h"
#include "server.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <memory>
#include <seastar/core/coroutine.hh>
#include <seastar/core/future.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/seastar.hh>
#include <seastar/core/temporary_buffer.hh>
#include <seastar/core/when_all.hh>
#include <seastar/net/socket_defs.hh>
#include <spdlog/spdlog.h>

static boost::uuids::random_generator generator;

namespace stanxx {

ConnectionTcp::ConnectionTcp (TransportTcp* server, seastar::connected_socket&& fd)
: _server (server) {
    fd.set_nodelay (true);
    _read_buf  = std::move (fd.input ());
    _write_buf = std::move (fd.output (64 * 1024));
    auto uuid  = generator ();
    _id        = boost::uuids::to_string (uuid);
    _cpuId     = seastar::this_shard_id ();
}

seastar::future<seastar::temporary_buffer<char>> ConnectionTcp::read () {
    return _input_buffer.pop_eventually ();
}

seastar::future<> ConnectionTcp::write (seastar::temporary_buffer<char> data) {
    return _output_buffer.push_eventually (std::move (data));
}

seastar::future<> ConnectionTcp::run () {
    try {
        co_await seastar::when_all (read_loop (), write_loop ());
    } catch (const std::exception& e) {
        spdlog::error ("processing failed: {}", e.what ());
    }
}

void ConnectionTcp::shutdown_input () {
    (void)_read_buf.close ();
}

seastar::future<> ConnectionTcp::close () {
    return seastar::make_ready_future ();
}

ConnectionTcp::~ConnectionTcp () {
    spdlog::info ("connection {} destroyed", _id);
    if (_server != nullptr)
        _server->_connections.erase (_server->_connections.iterator_to (*this));
}

seastar::future<> ConnectionTcp::read_loop () {
    SPDLOG_DEBUG ("reading loop");
    try {
        while (true) {
            auto data = co_await _read_buf.read ();
            if (data.size () == 0) {
                _input_buffer.push ({});
                _output_buffer.push ({});
                break;
            }
            _input_buffer.push (std::move (data));
        }
    } catch (const std::exception& e) {
        spdlog::error ("read_loop failed: {}", e.what ());
        _input_buffer.push ({});
        co_return;
    }
    co_await _read_buf.close ();
}

seastar::future<> ConnectionTcp::write_loop () {
    try {
        while (true) {
            auto data = co_await _output_buffer.pop_eventually ();
            if (data.size () == 0) {
                break;
            }
            co_await _write_buf.write (std::move (data));
            co_await _write_buf.flush ();
        }
    } catch (const std::exception& e) {
        spdlog::error ("write_loop failed: {}", e.what ());
        _input_buffer.push ({});
        co_return;
    }
    co_await _write_buf.close ();
}

TransportTcp::TransportTcp (ClusteredSubscriberManager* subscriberManager)
: _subscriberManager (subscriberManager) {
}

TransportTcp::~TransportTcp () {
}

seastar::future<> TransportTcp::listen (const std::string& address, int port) {
    _cpuId = seastar::this_shard_id ();
    seastar::socket_address sa (seastar::ipv4_addr (address, port));
    seastar::listen_options opts;
    opts.reuse_address = true;
    _listener          = seastar::listen (sa, opts);
    spdlog::info ("listening on {}:{}", address, port);

    return seastar::repeat ([this] () {
        SPDLOG_DEBUG ("waiting for connection");
        return _listener.accept ()
        .then ([this] (seastar::accept_result ar) {
            SPDLOG_DEBUG ("new connection accepted");
            (void)handleConnection (std::move (ar));
            return seastar::make_ready_future<seastar::stop_iteration> (
            seastar::stop_iteration::no);
        })
        .handle_exception ([this] (std::exception_ptr e) {
            return seastar::make_ready_future<seastar::stop_iteration> (
            seastar::stop_iteration::yes);
        });
    });
}

seastar::future<> TransportTcp::handleConnection (seastar::accept_result ar) {
    auto conn = std::make_unique<ConnectionTcp> (this, std::move (ar.connection));
    _connections.push_back (*conn);
    auto client = std::make_unique<Client> (
    conn->id (), conn->cpuId (), conn.get (), _subscriberManager);
    co_await seastar::when_all (conn->run (), client->run ());
}

seastar::future<> TransportTcp::close () {
    spdlog::info ("closing tcp server");
    _listener.abort_accept ();

    for (auto&& c : _connections) {
        c.shutdown_input ();
    }

    return _gate.close ().then ([this] {
        SPDLOG_DEBUG ("gate closed");

        return seastar::parallel_for_each (_connections, [] (ConnectionTcp& conn) {
            return conn.close ().handle_exception ([] (auto ignored) {});
        });
    });
}

seastar::future<> TransportTcp::stop () {
    spdlog::info ("stop tcp server");
    return seastar::make_ready_future ();
}

} // namespace stanxx
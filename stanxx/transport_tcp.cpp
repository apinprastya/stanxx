#include "transport_tcp.h"
#include "client.h"
#include "server.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <memory>
#include <seastar/core/do_with.hh>
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

Connection::Connection (TransportTcp& server, seastar::connected_socket&& fd)
: _server (server), _fd (std::move (fd)), _read_buf (_fd.input ()),
  _write_buf (_fd.output ()), _input_buffer{ QUEUE_SIZE }, _output_buffer{ QUEUE_SIZE } {
    auto uuid = generator ();
    _id       = boost::uuids::to_string (uuid);
    _cpuId    = seastar::this_shard_id ();
    _input    = seastar::input_stream<char>{ seastar::data_source{
    std::make_unique<connection_source_impl> (&_input_buffer) } };
    _output   = seastar::output_stream<char>{ seastar::data_sink{
    std::make_unique<connection_sink_impl> (&_output_buffer) } };
    on_new_connection ();
}

seastar::future<seastar::temporary_buffer<char>> Connection::read () {
    return _input.read ();
}

seastar::future<> Connection::write (seastar::temporary_buffer<char> data) {
    return _output.write (std::move (data));
}

seastar::future<> Connection::flush () {
    return _output.flush ();
}

seastar::future<> Connection::process (handler_t handler) {
    return seastar::when_all_succeed (read_loop (), write_loop (), handler (this))
    .discard_result ()
    .handle_exception ([] (const std::exception_ptr& e) {
        spdlog::error ("processing failed: {}", e);
    });
}

void Connection::shutdown_input () {
    _fd.shutdown_input ();
}

seastar::future<> Connection::close () {
    _done = true;
    return when_all_succeed (_input.close (), _output.close ())
    .discard_result ()
    .handle_exception ([] (const std::exception_ptr& e) {
        try {
            std::rethrow_exception (e);
        } catch (const std::system_error& se) {
            spdlog::error ("processing failed: std::system_error ({}): {}",
            se.code ().value (), se.what ());
        } catch (const std::exception& ex) {
            spdlog::error ("processing failed: {}", ex.what ());
        } catch (...) {
            spdlog::error ("processing failed: unknown exception");
        }
    })
    .finally ([this] { _fd.shutdown_output (); });
}

Connection::~Connection () {
    _server._connections.erase (_server._connections.iterator_to (*this));
}

seastar::future<> Connection::read_loop () {
    spdlog::debug ("reading loop");
    return seastar::do_until (
    [this] () { return _done; }, [this] { return read_one (); })
    .finally ([this] () {
        _output_buffer.push ({});
        return close ();
    });
}

seastar::future<> Connection::read_one () {
    return _read_buf.read ()
    .then ([this] (seastar::temporary_buffer<char> data) {
        if (data.size () == 0) {
            _done = true;
            return seastar::make_ready_future<> ();
        }
        _input_buffer.push (std::move (data));
        return seastar::make_ready_future<> ();
    })
    .handle_exception ([] (const std::exception_ptr& e) {
        spdlog::error ("read_loop failed: {}", e);
        return seastar::make_ready_future<> ();
    });
}

seastar::future<> Connection::write_loop () {
    return seastar::do_until ([this] () { return _done; },
    [this] () {
        return _output_buffer.pop_eventually ().then (
        [this] (seastar::temporary_buffer<char> data) {
            if (data.size () == 0) {
                return seastar::make_ready_future<> ();
            }
            return _write_buf.write (data.get (), data.size ()).then ([this] {
                return _write_buf.flush ();
            });
        });
    });
}

void Connection::on_new_connection () {
    _server._connections.push_back (*this);
}

TransportTcp::TransportTcp (Server* server) : _server (server) {
}

TransportTcp::~TransportTcp () {
}

seastar::future<> TransportTcp::listen (const std::string& address, int port) {
    _cpuId = seastar::this_shard_id ();
    seastar::socket_address sa (seastar::ipv4_addr (address, port));
    seastar::listen_options opts;
    opts.reuse_address = true;
    listener           = seastar::engine ().listen (sa, opts);
    spdlog::info ("listening on {}:{}", address, port);

    return seastar::repeat ([this] () {
        spdlog::debug ("waiting for connection");
        return listener.accept ()
        .then ([this] (seastar::accept_result ar) {
            spdlog::debug ("new connection accepted");
            auto conn = std::make_unique<Connection> (*this, std::move (ar.connection));
            (void)seastar::try_with_gate (gate, [conn = std::move (conn), this] () mutable {
                return seastar::do_with (std::move (conn), [this] (auto& conn) {
                    return conn->process ([this] (Connection* connection) {
                        auto client = std::make_unique<Client> (connection->id (),
                        connection->cpuId (), connection, _server);
                        return seastar::do_with (std::move (client),
                        [] (auto& client) { return client->run (); });
                    });
                });
            });
            return seastar::make_ready_future<seastar::stop_iteration> (
            seastar::stop_iteration::no);
        })
        .handle_exception ([this] (std::exception_ptr e) {
            spdlog::error ("error listening repeat {}", e);
            return seastar::make_ready_future<seastar::stop_iteration> (
            seastar::stop_iteration::yes);
        });
    });
}

seastar::future<> TransportTcp::stop () {
    spdlog::info ("closing tcp server");
    listener.abort_accept ();

    for (auto&& c : _connections) {
        c.shutdown_input ();
    }

    return gate.close ().then ([this] {
        spdlog::debug ("gate closed");

        return seastar::parallel_for_each (_connections, [] (Connection& conn) {
            return conn.close ().handle_exception ([] (auto ignored) {});
        });
    });
}

} // namespace stanxx
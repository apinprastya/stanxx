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

Connection::Connection (TransportTcp* server, seastar::connected_socket&& fd)
: _server (server) {
    //_fd        = std::move (fd);
    _read_buf  = std::move (fd.input ());
    _write_buf = std::move (fd.output ());
    auto uuid  = generator ();
    _id        = boost::uuids::to_string (uuid);
    _cpuId     = seastar::this_shard_id ();

    on_new_connection ();
}

seastar::future<seastar::temporary_buffer<char>> Connection::read () {
    return _input_buffer.pop_eventually ();
}

seastar::future<> Connection::write (seastar::temporary_buffer<char> data) {
    return _output_buffer.push_eventually (std::move (data));
}

seastar::future<> Connection::flush () {
    return _write_buf.flush ();
}

void Connection::writeAsync (seastar::temporary_buffer<char> data) {
    _output_buffer.push ({ std::move (data) });
}

seastar::future<> Connection::process (handler_t handler) {
    return seastar::when_all (read_loop (), write_loop (), handler (this))
    .discard_result ()
    .handle_exception ([] (const std::exception_ptr& e) {
        spdlog::error ("processing failed: {}", e);
    })
    .finally ([] () { spdlog::info ("connection process ends"); });
}

void Connection::shutdown_input () {
}

seastar::future<> Connection::close () {
    return seastar::make_ready_future ();
}

Connection::~Connection () {
    spdlog::info ("connection {} destroyed", _id);
    if (_server != nullptr)
        _server->_connections.erase (_server->_connections.iterator_to (*this));
}

seastar::future<> Connection::read_loop () {
    spdlog::debug ("reading loop");
    return seastar::repeat ([this] { return read_one (); }).finally ([this] () {
        _output_buffer.push ({});
        return _read_buf.close ();
    });
}

seastar::future<seastar::stop_iteration> Connection::read_one () {
    return _read_buf.read ()
    .then ([this] (seastar::temporary_buffer<char> data) {
        if (data.size () == 0) {
            _input_buffer.push ({});
            return seastar::make_ready_future<seastar::stop_iteration> (
            seastar::stop_iteration::yes);
        }
        _input_buffer.push (std::move (data));
        return seastar::make_ready_future<seastar::stop_iteration> (
        seastar::stop_iteration::no);
    })
    .handle_exception ([this] (const std::exception_ptr& e) {
        spdlog::error ("read_loop failed: {}", e);
        return _input_buffer
        .push_eventually (seastar::temporary_buffer<char> ())
        .then ([this] {
            return seastar::make_ready_future<seastar::stop_iteration> (
            seastar::stop_iteration::yes);
        });
    });
}

seastar::future<> Connection::write_loop () {
    return seastar::repeat ([this] () {
        return _output_buffer.pop_eventually ().then (
        [this] (seastar::temporary_buffer<char> data) {
            if (data.size () == 0) {
                return seastar::make_ready_future<seastar::stop_iteration> (
                seastar::stop_iteration::yes);
            }
            return _write_buf.write (std::move (data)).then ([this] {
                return _write_buf.flush ().then ([this] () {
                    return seastar::make_ready_future<seastar::stop_iteration> (
                    seastar::stop_iteration::no);
                });
            });
        });
    })
    .finally ([this] () { return _write_buf.close (); });
}

void Connection::on_new_connection () {
    if (_server != nullptr)
        _server->_connections.push_back (*this);
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
    listener           = seastar::listen (sa, opts);
    spdlog::info ("listening on {}:{}", address, port);

    return seastar::repeat ([this] () {
        spdlog::debug ("waiting for connection");
        return listener.accept ()
        .then ([this] (seastar::accept_result ar) {
            spdlog::debug ("new connection accepted");
            (void)seastar::do_with (
            std::make_unique<Connection> (this, std::move (ar.connection)),
            [this] (auto& conn) {
                return conn->process ([&conn, this] (Connection* connection) mutable {
                    return seastar::do_with (
                    std::make_unique<Client> (connection->id (),
                    connection->cpuId (), connection, _server),
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
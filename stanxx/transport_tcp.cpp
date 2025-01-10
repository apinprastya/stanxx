#include "transport_tcp.h"
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

namespace stanxx {

seastar::future<> Connection::process () {
    return seastar::when_all_succeed (read_loop (), write_loop ())
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
    .finally ([this] {
        spdlog::info ("connection finish");
        _fd.shutdown_output ();
    });
}

Connection::~Connection () {
    spdlog::info ("Connection exiting");
    _server._connections.erase (_server._connections.iterator_to (*this));
}

seastar::future<> Connection::read_loop () {
    spdlog::info ("reading loop");
    return seastar::do_until (
    [this] () { return _done; }, [this] { return read_one (); })
    .finally ([this] () { _output_buffer.push ({}); });
}

seastar::future<> Connection::read_one () {
    spdlog::info ("read one");
    return _read_buf.read ()
    .then ([this] (seastar::temporary_buffer<char> data) {
        spdlog::info ("new data: {} {} {}",
        std::string (data.get (), data.size ()), data.size (), _read_buf.eof ());
        if (_read_buf.eof ()) {
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
    spdlog::info ("write loop");
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
    spdlog::info ("on_new_connection");
    _server._connections.push_back (*this);
    char* data =
    "INFO "
    "{\"server_id\":"
    "\"NCEUKVMQR4KCNGMKEAIEFS5OF4VMI34DXCTZ5HBFR4YSLETPHFDEWIRQ\",\"server_"
    "name\":\"NCEUKVMQR4KCNGMKEAIEFS5OF4VMI34DXCTZ5HBFR4YSLETPHFDEWIRQ\","
    "\"version\":\"2.11.0-dev\",\"proto\" : 1,\"go\" : "
    "\"go1.23.3\",\"host\" : \"0.0.0.0\",\"port\" : 4222,\"headers\" : "
    "true,\"max_payload\" : "
    "1048576,\"client_id\":5,\"client_ip\":\"127.0.0.1\",\"xkey\":"
    "\"XBRNVBBFW45EB3RA7JI3D6HU6ROXESE2EU2IXXTWYOCENKIGI5AW2GU2\"}\r\n";
    (void)_output_buffer.push_eventually (
    seastar::temporary_buffer<char> (data, strlen (data)));
}

TransportTcp::~TransportTcp () {
}

seastar::future<> TransportTcp::listen (const std::string& address, int port) {
    seastar::socket_address sa (seastar::ipv4_addr (address, port));
    seastar::listen_options opts;
    opts.reuse_address = true;
    listener           = seastar::engine ().listen (sa, opts);
    spdlog::info ("listening on {}:{}", address, port);

    return seastar::repeat ([this] () {
        spdlog::info ("waiting for connection");
        return listener.accept ()
        .then ([this] (seastar::accept_result ar) {
            spdlog::info ("new connection accepted");
            auto conn = std::make_unique<Connection> (*this, std::move (ar.connection));
            (void)seastar::try_with_gate (gate,
            [conn = std::move (conn)] () mutable {
                return seastar::do_with (std::move (conn), [] (auto& conn) {
                    return conn->process ().finally ([conn = std::move (conn)] () {
                        spdlog::info ("connection done 123");
                    });
                });
            })
            .finally ([] () { spdlog::info ("connection done"); });
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

seastar::future<> TransportTcp::close () {
    spdlog::info ("closing tcp server");
    listener.abort_accept ();
    return gate.close ().then ([this] {
        spdlog::info ("gate closed");
        return seastar::parallel_for_each (_connections, [] (Connection& conn) {
            return conn.close ().handle_exception ([] (auto ignored) {});
        });
    });
}

} // namespace stanxx
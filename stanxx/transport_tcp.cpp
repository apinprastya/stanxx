#include "transport_tcp.h"
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
    .finally ([this] { _fd.shutdown_output (); });
}

Connection::~Connection () {
}

seastar::future<> Connection::read_loop () {
    spdlog::info ("reading loop");
    return seastar::do_until (
    [this] () { return _done; }, [this] { return read_one (); });
}

seastar::future<> Connection::read_one () {
    spdlog::info ("read one");
    return _read_buf.read ()
    .then ([this] (seastar::temporary_buffer<char> data) {
        if (data.size () == 0) {
            _done = true;
            return seastar::make_ready_future<> ();
        }
        spdlog::info ("new data: {}", std::string (data.get (), data.size ()));
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
            return _write_buf.write (data.get (), data.size ()).then ([this] {
                return _write_buf.flush ();
            });
        });
    });
}

void Connection::on_new_connection () {
}

TransportTcp::~TransportTcp () {
    spdlog::info ("TransportTcp::~TransportTcp");
}


seastar::future<> TransportTcp::listen (const std::string& address, int port) {
    seastar::socket_address sa (seastar::ipv4_addr (address, port));
    seastar::listen_options opts;
    opts.reuse_address = true;
    listener           = seastar::engine ().listen (sa, opts);
    spdlog::info ("listening on {}:{}", address, port);
    return seastar::try_with_gate (gate, [this] () {
        return seastar::repeat ([this] () {
            spdlog::info ("waiting for connection");
            return listener.accept ()
            .then ([this] (seastar::accept_result ar) {
                auto conn =
                std::make_shared<Connection> (*this, std::move (ar.connection));
                (void)seastar::try_with_gate (
                gate, [conn = std::move (conn)] () { return conn->process (); });
                return seastar::make_ready_future<seastar::stop_iteration> (
                seastar::stop_iteration::no);
            })
            .handle_exception ([this] (std::exception_ptr e) {
                return seastar::make_ready_future<seastar::stop_iteration> (
                seastar::stop_iteration::yes);
            });
        });
    });
}

seastar::future<> TransportTcp::close () {
    return gate.close ().then ([this] { return listener.abort_accept (); });
}


} // namespace stanxx
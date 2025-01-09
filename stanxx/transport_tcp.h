#pragma once

#include <boost/intrusive/list_hook.hpp>
#include <memory>
#include <seastar/core/gate.hh>
#include <seastar/core/iostream.hh>
#include <seastar/core/queue.hh>
#include <seastar/core/resource.hh>
#include <seastar/net/api.hh>

#define QUEUE_SIZE 512

namespace stanxx {

class TransportTcp;

class Connection : public boost::intrusive::list_base_hook<> {
    using buff_t = seastar::temporary_buffer<char>;

    class connection_source_impl final : public seastar::data_source_impl {

        seastar::queue<buff_t>* data;

        public:
        connection_source_impl (seastar::queue<buff_t>* data) : data (data) {
        }

        virtual seastar::future<buff_t> get () override {
            return data->pop_eventually ().then_wrapped ([] (seastar::future<buff_t> f) {
                try {
                    return make_ready_future<buff_t> (std::move (f.get ()));
                } catch (...) {
                    return seastar::current_exception_as_future<buff_t> ();
                }
            });
        }

        virtual seastar::future<> close () override {
            data->push (buff_t (0));
            return seastar::make_ready_future<> ();
        }
    };

    class connection_sink_impl final : public seastar::data_sink_impl {
        seastar::queue<buff_t>* data;

        public:
        connection_sink_impl (seastar::queue<buff_t>* data) : data (data) {
        }

        virtual seastar::future<> put (seastar::net::packet d) override {
            seastar::net::fragment f = d.frag (0);
            return data->push_eventually (
            seastar::temporary_buffer<char>{ std::move (f.base), f.size });
        }

        size_t buffer_size () const noexcept override {
            return data->max_size ();
        }

        virtual seastar::future<> close () override {
            data->push (buff_t (0));
            return seastar::make_ready_future<> ();
        }
    };

    TransportTcp& _server;
    seastar::connected_socket _fd;
    seastar::input_stream<char> _read_buf;
    seastar::output_stream<char> _write_buf;
    bool _done = false;

    seastar::queue<seastar::temporary_buffer<char>> _input_buffer;
    seastar::input_stream<char> _input;
    seastar::queue<seastar::temporary_buffer<char>> _output_buffer;
    seastar::output_stream<char> _output;

    public:
    /*!
     * \param server owning \ref server
     * \param fd established socket used for communication
     */
    Connection (TransportTcp& server, seastar::connected_socket&& fd)
    : _server (server), _fd (std::move (fd)), _read_buf (_fd.input ()),
      _write_buf (_fd.output ()), _input_buffer{ QUEUE_SIZE },
      _output_buffer{ QUEUE_SIZE } {
        _input  = seastar::input_stream<char>{ seastar::data_source{
        std::make_unique<connection_source_impl> (&_input_buffer) } };
        _output = seastar::output_stream<char>{ seastar::data_sink{
        std::make_unique<connection_sink_impl> (&_output_buffer) } };
        on_new_connection ();
    }
    ~Connection ();

    seastar::future<> process ();
    void shutdown_input ();
    seastar::future<> close ();

    protected:
    seastar::future<> read_loop ();
    seastar::future<> read_one ();
    seastar::future<> write_loop ();
    void on_new_connection ();
};

class TransportTcp : public std::enable_shared_from_this<TransportTcp> {
    public:
    ~TransportTcp ();
    seastar::future<> listen (const std::string& address, int port);
    seastar::future<> close ();

    private:
    seastar::server_socket listener;
    seastar::gate gate;
};

} // namespace stanxx
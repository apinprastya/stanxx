#include "client.h"
#include "buffer.h"
#include "server.h"
#include "subscriber.h"
#include "transport_tcp.h"
#include <memory>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <seastar/core/do_with.hh>
#include <seastar/core/future.hh>
#include <seastar/core/iostream.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/timer.hh>
#include <seastar/core/when_all.hh>
#include <spdlog/spdlog.h>
#include <utility>

std::string quote (const std::string& str) {
    std::ostringstream oss;
    oss << '"';
    for (char c : str) {
        switch (c) {
        case '\\': oss << "\\\\"; break;
        case '"': oss << "\\\""; break;
        case '\n': oss << "\\n"; break;
        case '\r': oss << "\\r"; break;
        case '\t': oss << "\\t"; break;
        default:
            if (std::isprint (static_cast<unsigned char> (c))) {
                oss << c;
            } else {
                oss << "\\x" << std::hex << std::setw (2) << std::setfill ('0')
                    << (static_cast<int> (c) & 0xFF);
            }
            break;
        }
    }
    oss << '"';
    return oss.str ();
}

namespace stanxx {

Client::Client (const std::string& id, int cpuId, Connection* connection, SubscriberManagerHandler* subscriberManagerHandler)
: _id (id), _cpuId (cpuId), _connection (connection),
  _subscriberManagerHandler (subscriberManagerHandler) {
    _parser = std::make_unique<MessageParser> (this);
}

Client::~Client () {
    _subscriberManagerHandler->getSubscriberManager ()->unsubscribeClientId (_id);
    if (_pingTimer) {
        _pingTimer->cancel ();
        _pingTimer = nullptr;
    }
}

seastar::future<> Client::run () {
    if (_connection != nullptr) {
        // first we need to send the welcome message
        static char* data =
        "INFO "
        "{\"server_id\":"
        "\"NCEUKVMQR4KCNGMKEAIEFS5OF4VMI34DXCTZ5HBFR4YSLETPHFDEWIRQ\",\"server_"
        "name\":\"NCEUKVMQR4KCNGMKEAIEFS5OF4VMI34DXCTZ5HBFR4YSLETPHFDEWIRQ\","
        "\"version\":\"2.11.0-dev\",\"proto\" : 1,\"go\" : "
        "\"go1.23.3\",\"host\" : \"0.0.0.0\",\"port\" : 4222,\"headers\" : "
        "true,\"max_payload\" : "
        "1048576,\"client_id\":5,\"client_ip\":\"127.0.0.1\",\"xkey\":"
        "\"XBRNVBBFW45EB3RA7JI3D6HU6ROXESE2EU2IXXTWYOCENKIGI5AW2GU2\"}\r\n";
        (void)_connection->write (seastar::temporary_buffer<char> (data, strlen (data)));
        return loopRead ();
    }
    return seastar::make_exception_future (
    std::runtime_error ("connection is null"));
}

seastar::future<> Client::loopRead () {
    return seastar::repeat ([this] () {
        return _connection->read ().then ([this] (seastar::temporary_buffer<char> data) {
            spdlog::info ("client new data: {}: {}", data.size (),
            quote (std::string (data.get (), data.size ())));
            if (data.size () == 0) {
                return seastar::make_ready_future<seastar::stop_iteration> (
                seastar::stop_iteration::yes);
            }
            return _parser->parseMessage (std::move (data)).then ([this] (auto result) {
                if (result.has_value ()) {
                    spdlog::error (
                    "error parsing message: {}", result.value ().errorString ());
                }
                return seastar::make_ready_future<seastar::stop_iteration> (
                seastar::stop_iteration::no);
            });
        });
    });
}

seastar::future<> Client::processConnect (seastar::temporary_buffer<char> data) {
    spdlog::debug ("Connect args: {}", data.get ());

    auto dataJson = nlohmann::json::parse (data.begin (), data.end (), nullptr, false);
    if (dataJson.is_discarded ()) {
        spdlog::error (
        "unable to parse json: {}", std::string (data.begin (), data.end ()));
        return seastar::make_exception_future (
        std::runtime_error ("unable to parse json"));
    }
    auto reqArg = dataJson.get<ClientOpts> ();
    spdlog::debug ("{} {} {}", reqArg.name, reqArg.lang, reqArg.version);

    if (!_pingTimer) {
        _pingTimer = std::make_shared<seastar::timer<>> ();
    }

    _pingTimer->set_callback ([this] () { (void)sendPing (); });
    // TODO: set the ping interval from the client opts
    _pingTimer->arm_periodic (std::chrono::seconds{ 5 });

    return seastar::make_ready_future<> ();
}

seastar::future<> Client::processPing () {
    spdlog::debug ("process ping message");
    constexpr const char* pongMessage = "PONG\r\n";
    const std::size_t length          = std::strlen (pongMessage);
    return _connection->write (seastar::temporary_buffer<char> (pongMessage, length));
}

seastar::future<> Client::sendPing () {
    spdlog::debug ("sending ping");
    constexpr const char* pongMessage = "PING\r\n";
    const std::size_t length          = std::strlen (pongMessage);
    return _connection->write (seastar::temporary_buffer<char> (pongMessage, length));
    _roundTrip.setStartToNow ();
}

seastar::future<> Client::sendError (const std::string& err) {
    spdlog::debug ("sending error");
    auto messageStr = fmt::format ("-ERR '{}'\r\n", err);
    return _connection->write (
    seastar::temporary_buffer<char> (messageStr.data (), messageStr.size ()));
}

seastar::future<> Client::sendOK () {
    spdlog::debug ("sending ok");
    constexpr const char* pongMessage = "+OK\r\n";
    const std::size_t length          = std::strlen (pongMessage);
    return _connection->write (seastar::temporary_buffer<char> (pongMessage, length));
}

seastar::future<> Client::processPong () {
    spdlog::debug ("got pong message");
    _roundTrip.calculateRrt ();
    return seastar::make_ready_future<> ();
}

seastar::future<> Client::processSubscribe (const std::vector<std::string_view>& args) {
    if (args.size () == 2)
        spdlog::debug ("got subscibe message: {} : {}", args[0], args[1]);
    else if (args.size () == 3)
        spdlog::debug ("got subscibe message: {} : {} : {}", args[0], args[1], args[2]);
    auto subject    = std::string (args[0]);
    auto subId      = std::string (args[1]);
    auto subscriber = std::make_shared<Subscriber> (subject, subId, this);
    _subscriberManagerHandler->getSubscriberManager ()->addSubscriber (subscriber);
    return seastar::make_ready_future<> ();
}

seastar::future<> Client::processPublish (const PublishArg& publishArg,
seastar::temporary_buffer<char> data) {
    spdlog::debug ("publish subject: {}; reply: {}; data: {}", publishArg.subject,
    publishArg.reply, std::string (data.begin (), data.end ()));
    auto subscribers = _subscriberManagerHandler->getSubscriberManager ()->getSubscriber (
    publishArg.subject);
    spdlog::debug ("subscriber length: {}", subscribers.size ());
    if (subscribers.size () == 0) {
        return seastar::make_ready_future<> ();
    }
    for (const auto& subcriber : subscribers) {
        CharBuffer buffer;
        buffer.write ("MSG ");
        buffer.write (publishArg.subject);
        buffer.write (" ");
        buffer.write (subcriber->getId ());
        if (!publishArg.reply.empty ()) {
            buffer.write (" ");
            buffer.write (publishArg.reply);
        }
        buffer.write (" ");
        buffer.write (std::to_string (data.size ()));
        buffer.write ("\r\n");
        buffer.write (data.get (), data.size ());
        buffer.write ("\r\n");

        auto bufferStr = buffer.getBuffer ();
        spdlog::debug (
        "buffer value: {}", std::string{ bufferStr.begin (), bufferStr.end () });
        auto& buffData = buffer.getBuffer ();
        (void)subcriber->getClient ()->sendMessage (
        seastar::temporary_buffer<char> (buffData.data (), buffData.size ()));
    }
    return seastar::make_ready_future<> ();
}

seastar::future<> Client::sendMessage (seastar::temporary_buffer<char> data) {
    return _connection->write (std::move (data));
}

} // namespace stanxx
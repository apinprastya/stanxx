#include "client.h"
#include "buffer.h"
#include "server.h"
#include "subscriber.h"
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <seastar/core/coroutine.hh>
#include <seastar/core/do_with.hh>
#include <seastar/core/future.hh>
#include <seastar/core/iostream.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/shard_id.hh>
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

Client::Client (const std::string& id, int cpuId, Connection* connection, ClusteredSubscriberManager* subscriberManager)
: _id (id), _cpuId (cpuId), _connection (connection),
  _subscriberManager (subscriberManager) {
    _parser = std::make_unique<MessageParser> (this);
}

Client::~Client () {
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
        co_await _connection->write (
        seastar::temporary_buffer<char> (data, strlen (data)));
        try {
            while (true) {
                auto data = co_await _connection->read ();
                if (data.size () == 0) {
                    break;
                }
                auto result = co_await _parser->parseMessage (std::move (data));
                if (result.has_value ()) {
                    spdlog::error (
                    "error parsing message: {}", result.value ().errorString ());
                }
            }
        } catch (const std::exception& e) {
            spdlog::error ("error reading data: {}", e.what ());
        }
    }
    co_await _subscriberManager->unsubscribeClientId (_id);
}

seastar::future<> Client::processError (std::string_view errString) {
    spdlog::error ("client id: {}, error message: {}", _id, errString);
    return seastar::make_ready_future ();
}

seastar::future<> Client::processConnect (std::span<const char> data) {
    SPDLOG_DEBUG ("Connect args: {}", data.data ());

    auto dataJson = nlohmann::json::parse (data.begin (), data.end (), nullptr, false);
    if (dataJson.is_discarded ()) {
        spdlog::error (
        "unable to parse json: {}", std::string (data.begin (), data.end ()));
        return seastar::make_ready_future<> ();
    }
    auto reqArg = dataJson.get<ClientOpts> ();
    SPDLOG_DEBUG ("{} {} {}", reqArg.name, reqArg.lang, reqArg.version);

    if (!_pingTimer) {
        _pingTimer = std::make_shared<seastar::timer<>> ();
    }

    _pingTimer->set_callback ([this] () { (void)sendPing (); });
    // TODO: set the ping interval from the client opts
    _pingTimer->arm_periodic (std::chrono::seconds{ 5 });
    return seastar::make_ready_future<> ();
}

seastar::future<> Client::processPing () {
    SPDLOG_DEBUG ("process ping message");
    constexpr const char* pongMessage = "PONG\r\n";
    const std::size_t length          = std::strlen (pongMessage);
    return _connection->write (seastar::temporary_buffer<char> (pongMessage, length));
}

seastar::future<> Client::sendPing () {
    SPDLOG_DEBUG ("sending ping");
    constexpr const char* pongMessage = "PING\r\n";
    const std::size_t length          = std::strlen (pongMessage);
    _roundTrip.setStartToNow ();
    return _connection->write (seastar::temporary_buffer<char> (pongMessage, length));
}

seastar::future<> Client::sendError (const std::string& err) {
    SPDLOG_DEBUG ("sending error");
    auto messageStr = fmt::format ("-ERR '{}'\r\n", err);
    return _connection->write (
    seastar::temporary_buffer<char> (messageStr.data (), messageStr.size ()));
}

seastar::future<> Client::sendOK () {
    SPDLOG_DEBUG ("sending ok");
    constexpr const char* pongMessage = "+OK\r\n";
    const std::size_t length          = std::strlen (pongMessage);
    return _connection->write (seastar::temporary_buffer<char> (pongMessage, length));
}

seastar::future<> Client::processPong () {
    SPDLOG_DEBUG ("got pong message");
    _roundTrip.calculateRrt ();
    return seastar::make_ready_future<> ();
}

seastar::future<> Client::processSubscribe (const std::vector<std::string_view>& args) {
    if (args.size () == 2)
        SPDLOG_DEBUG ("got subscibe message: {} : {}", args[0], args[1]);
    else if (args.size () == 3)
        SPDLOG_DEBUG ("got subscibe message: {} : {} : {}", args[0], args[1], args[2]);
    auto subject = std::string (args[0]);
    auto subId   = std::string (args[1]);
    co_await _subscriberManager->addSubscriber (
    subject, subId, seastar::this_shard_id (), this);
}

seastar::future<> Client::processPublish (const PublishArg& publishArg,
std::span<const char> data) {
    SPDLOG_DEBUG ("publish subject: {}; reply: {}; data: {}", publishArg.subject,
    publishArg.reply, std::string (data.begin (), data.end ()));
    auto subscribers = _subscriberManager->getSubscriber (publishArg.subject);
    SPDLOG_DEBUG ("subscriber length: {}", subscribers.size ());
    if (subscribers.size () == 0) {
        co_return;
    }
    CharBuffer buffer;
    for (const auto& subcriber : subscribers) {
        buffer.clear ();
        buffer.reserve (64 * 1024);
        buffer.write ("MSG ", 4);
        buffer.write (publishArg.subject.data (), publishArg.subject.size ());
        buffer.write (" ", 1);
        buffer.write (subcriber->getId ().data (), subcriber->getId ().size ());
        if (!publishArg.reply.empty ()) {
            buffer.write (" ", 1);
            buffer.write (publishArg.reply.data (), publishArg.reply.size ());
        }
        buffer.write (" ", 1);
        auto dataSizeStr = std::to_string (data.size ());
        buffer.write (dataSizeStr.data (), dataSizeStr.size ());
        buffer.write ("\r\n", 2);
        buffer.write (data.data (), data.size ());
        buffer.write ("\r\n", 2);
        auto bufferData = std::move (buffer).getBuffer ();
        (void)_subscriberManager->sendMessage (subcriber,
        seastar::temporary_buffer<char> (bufferData.data (), bufferData.size ()));
    }
}

seastar::future<> Client::sendMessage (seastar::temporary_buffer<char> data) {
    return _connection->write (std::move (data));
}

} // namespace stanxx
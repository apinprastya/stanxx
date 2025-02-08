#include "client.h"
#include "buffer.h"
#include "server.h"
#include "subscriber.h"
#include "transport_tcp.h"
#include <memory>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <span>
#include <spdlog/spdlog.h>
#include <vector>

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
    if (_subscriberManagerHandler != nullptr)
        _subscriberManagerHandler->getSubscriberManager ()->unsubscribeClientId (_id);
}

asio::awaitable<void> Client::run () {
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
    } else {
        throw std::runtime_error ("connection is null");
    }
}

asio::awaitable<void> Client::loopRead () {
}

void Client::read (std::span<char> data) {
    /*spdlog::debug ("client new data: {}: {}", data.size (),
    quote (std::string (data.data (), data.size ())));*/
    auto result = _parser->parseMessage (data);
    if (result.has_value ()) {
        spdlog::error ("error parsing message: {}", result.value ().errorString ());
    }
}

void Client::processConnect (std::span<char> data) {
    spdlog::debug ("Connect args: {}", data.data ());

    auto dataJson = nlohmann::json::parse (data.begin (), data.end (), nullptr, false);
    if (dataJson.is_discarded ()) {
        spdlog::error (
        "unable to parse json: {}", std::string (data.begin (), data.end ()));
        return;
    }
    auto reqArg = dataJson.get<ClientOpts> ();
    spdlog::debug ("{} {} {}", reqArg.name, reqArg.lang, reqArg.version);

    /*if (!_pingTimer) {
        _pingTimer = std::make_shared<seastar::timer<>> ();
    }

    _pingTimer->set_callback ([this] () { (void)sendPing (); });
    // TODO: set the ping interval from the client opts
    _pingTimer->arm_periodic (std::chrono::seconds{ 5 });*/
}

void Client::processPing () {
    spdlog::debug ("process ping message");
    constexpr const char* pongMessage = "PONG\r\n";
    const std::size_t length          = std::strlen (pongMessage);
    _connection->queue (std::vector<char> (pongMessage, pongMessage + length));
}

void Client::sendPing () {
    spdlog::debug ("sending ping");
    constexpr const char* pongMessage = "PING\r\n";
    const std::size_t length          = std::strlen (pongMessage);
    _connection->queue (std::vector<char> (pongMessage, pongMessage + length));
    _roundTrip.setStartToNow ();
}

void Client::sendError (const std::string& err) {
    spdlog::debug ("sending error");
    auto messageStr = fmt::format ("-ERR '{}'\r\n", err);
    _connection->queue (std::vector<char> (messageStr.begin (), messageStr.end ()));
}

void Client::sendOK () {
    spdlog::debug ("sending ok");
    constexpr const char* okMessage = "+OK\r\n";
    const std::size_t length        = std::strlen (okMessage);
    _connection->queue (std::vector<char> (okMessage, okMessage + length));
}

void Client::processPong () {
    spdlog::debug ("got pong message");
    _roundTrip.calculateRrt ();
}

void Client::processSubscribe (const std::vector<std::string_view>& args) {
    if (args.size () == 2)
        spdlog::debug ("got subscibe message: {} : {}", args[0], args[1]);
    else if (args.size () == 3)
        spdlog::debug ("got subscibe message: {} : {} : {}", args[0], args[1], args[2]);
    auto subject    = std::string (args[0]);
    auto subId      = std::string (args[1]);
    auto subscriber = std::make_shared<Subscriber> (subject, subId, this);
    _subscriberManagerHandler->getSubscriberManager ()->addSubscriber (subscriber);
}

void Client::processPublish (const PublishArg& publishArg, std::span<char> data) {
    spdlog::debug ("publish subject: {}; reply: {}; data: {}", publishArg.subject,
    publishArg.reply, std::string (data.begin (), data.end ()));
    auto subscribers = _subscriberManagerHandler->getSubscriberManager ()->getSubscriber (
    publishArg.subject);
    spdlog::debug ("subscriber length: {}", subscribers.size ());
    if (subscribers.size () == 0) {
        return;
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
        buffer.write (data.data (), data.size ());
        buffer.write ("\r\n");

        auto bufferStr = buffer.getBuffer ();
        spdlog::debug (
        "buffer value: {}", std::string{ bufferStr.begin (), bufferStr.end () });
        subcriber->getClient ()->sendMessage (buffer.getBuffer ());
    }
}

void Client::sendMessage (const std::span<const char>& data) {
    _connection->queue (std::vector<char> (data.begin (), data.end ()));
}

} // namespace stanxx
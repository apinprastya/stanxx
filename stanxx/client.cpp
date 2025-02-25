#include "client.h"
#include "buffer.h"
#include "server.h"
#include "subscriber.h"
#include "transport_tcp.h"
#include <iostream>
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


void Client::read (const std::span<char>& data) {
    /*spdlog::debug ("client new data: {}: {}", data.size (),
    quote (std::string (data.data (), data.size ())));*/
    // auto start  = std::chrono::high_resolution_clock::now ();
    auto result = _parser->parseMessage (data);
    if (result.has_value ()) {
        spdlog::error ("error parsing message: {}", result.value ().errorString ());
    }
    /*auto end = std::chrono::high_resolution_clock::now ();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds> (end - start);

    std::cout << "Execution time: " << duration.count () << " microseconds\n";*/
}

void Client::processConnect (const std::span<const char>& data) {
    SPDLOG_DEBUG ("Connect args: {}", data.data ());

    auto dataJson = nlohmann::json::parse (data.begin (), data.end (), nullptr, false);
    if (dataJson.is_discarded ()) {
        spdlog::error (
        "unable to parse json: {}", std::string (data.begin (), data.end ()));
        return;
    }
    auto reqArg = dataJson.get<ClientOpts> ();
    SPDLOG_DEBUG ("{} {} {}", reqArg.name, reqArg.lang, reqArg.version);
}

void Client::processPing () {
    SPDLOG_DEBUG ("process ping message");
    constexpr const char* pongMessage = "PONG\r\n";
    const std::size_t length          = std::strlen (pongMessage);
    _connection->queue (std::vector<char> (pongMessage, pongMessage + length));
}

void Client::sendPing () {
    SPDLOG_DEBUG ("sending ping");
    constexpr const char* pongMessage = "PING\r\n";
    const std::size_t length          = std::strlen (pongMessage);
    _connection->queue (std::vector<char> (pongMessage, pongMessage + length));
    _roundTrip.setStartToNow ();
}

void Client::sendError (const std::string& err) {
    SPDLOG_DEBUG ("sending error");
    auto messageStr = fmt::format ("-ERR '{}'\r\n", err);
    _connection->queue (std::vector<char> (messageStr.begin (), messageStr.end ()));
}

void Client::sendOK () {
    SPDLOG_DEBUG ("sending ok");
    constexpr const char* okMessage = "+OK\r\n";
    const std::size_t length        = std::strlen (okMessage);
    _connection->queue (std::vector<char> (okMessage, okMessage + length));
}

void Client::processPong () {
    SPDLOG_DEBUG ("got pong message");
    _roundTrip.calculateRrt ();
}

void Client::processSubscribe (const std::vector<std::string_view>& args) {
    if (args.size () == 2)
        SPDLOG_DEBUG ("got subscibe message: {} : {}", args[0], args[1]);
    else if (args.size () == 3)
        SPDLOG_DEBUG ("got subscibe message: {} : {} : {}", args[0], args[1], args[2]);
    auto subject    = std::string (args[0]);
    auto subId      = std::string (args[1]);
    auto subscriber = std::make_shared<Subscriber> (subject, subId, this);
    _subscriberManagerHandler->getSubscriberManager ()->addSubscriber (subscriber);
}

void Client::processPublish (const PublishArg& publishArg,
const std::span<const char>& data) {
    /*spdlog::debug ("publish subject: {}; reply: {}; data: {}", publishArg.subject,
    publishArg.reply, std::string (data.begin (), data.end ()));*/
    auto subscribers = _subscriberManagerHandler->getSubscriberManager ()->getSubscriber (
    publishArg.subject);
    // spdlog::debug ("subscriber length: {}", subscribers.size ());
    if (subscribers.size () == 0) {
        return;
    }
    for (const auto& subcriber : subscribers) {
        CharBuffer buffer;
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
        subcriber->getClient ()->sendMessage (std::move (buffer).getBuffer ());
    }
}

void Client::sendMessage (std::vector<char>&& data) {
    _connection->queue (std::move (data));
}

} // namespace stanxx
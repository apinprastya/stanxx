#pragma once

#include "transport_tcp.h"

#include <asio.hpp>
#include <memory>
#include <nlohmann/json.hpp>

namespace stanxx {

class SubscriberManager;

struct Info {
    std::string id{};
    std::string name{};
    std::string version{};
    int proto{};
    std::string host{};
    int port{};
    bool headers{};
    int maxPayload{};
    std::string clientIp{};
    int clientId{};

    friend void to_json (nlohmann::json& j, const Info& p) {
        j = nlohmann::json{
            { "id", p.id },
            { "name", p.name },
            { "version", p.version },
            { "proto", p.proto },
            { "host", p.host },
            { "port", p.port },
            { "headers", p.headers },
            { "max_payload", p.maxPayload },
            { "client_ip", p.clientIp },
            { "client_id", p.clientId },
        };
    }
};

class SubscriberManagerHandler {
    public:
    virtual SubscriberManager* getSubscriberManager () = 0;
};

class Server : public SubscriberManagerHandler {
    public:
    Server ();
    ~Server ();
    void run (int argc, char** argv);

    inline SubscriberManager* getSubscriberManager () override {
        return _subscribeManager.get ();
    }

    private:
    asio::io_context _ioContext;
    std::unique_ptr<SubscriberManager> _subscribeManager;
};
} // namespace stanxx
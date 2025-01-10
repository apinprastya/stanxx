#pragma once

#include "transport_tcp.h"

#include <memory>
#include <nlohmann/json.hpp>
#include <seastar/core/app-template.hh>
#include <seastar/core/future.hh>
#include <seastar/core/sharded.hh>
#include <seastar/net/api.hh>

namespace stanxx {


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

class Server {
    public:
    Server ();
    ~Server ();
    void run (int argc, char** argv);

    private:
    seastar::app_template app;
    seastar::sharded<TransportTcp> mainTransport;

    seastar::future<> handle_client (seastar::connected_socket conn);
};
} // namespace stanxx
#include "server.h"
#include "transport_tcp.h"
#include <csignal>
#include <seastar/core/reactor.hh>
#include <seastar/core/signal.hh>
#include <seastar/net/api.hh>

namespace stanxx {

Server::Server () {
    //mainTransport = std::make_unique<TransportTcp> ();
}

Server::~Server () {
}

void print_variables_map(const boost::program_options::variables_map& vm) {
    for (const auto& it : vm) {
        const auto& key = it.first;
        const auto& value = it.second.value();

        std::cout << key << ": ";

        if (auto v = boost::any_cast<std::string>(&value)) {
            std::cout << *v;
        } else if (auto v = boost::any_cast<int>(&value)) {
            std::cout << *v;
        } else if (auto v = boost::any_cast<double>(&value)) {
            std::cout << *v;
        } else if (auto v = boost::any_cast<bool>(&value)) {
            std::cout << std::boolalpha << *v;
        } else if (auto v = boost::any_cast<std::vector<std::string>>(&value)) {
            for (const auto& s : *v) {
                std::cout << s << " ";
            }
        } else {
            std::cout << "Unknown type";
        }

        std::cout << std::endl;
    }
}

void Server::run (int argc, char** argv) {
    app.run (argc, argv, [this] {
        app.configuration ();
        auto& opts    = app.configuration ();
        print_variables_map (opts);
        //std::cout << "Seastar configured with " << opts["memory"].value () << " bytes of memory" << std::endl;
    /*size_t memory = opts["memory"].as<size_t> ();
    std::cout << "Seastar configured with " << memory << " bytes of memory" << std::endl;
        std::cout << "Seastar configured with " << memory << " bytes of memory" << std::endl;*/
        return seastar::do_with(
            seastar::listen(seastar::make_ipv4_address({0x0, 4222})), [this](auto& listener) {
                return seastar::keep_doing([this, &listener] {
                    return listener.accept().then([this](seastar::accept_result ar) {
                        auto conn = std::move(ar.connection);
                        return handle_client(std::move(conn)).then([] {
                            return seastar::make_ready_future ();
                        });
                    });
                });
            });
    });
}

seastar::future<> Server::handle_client (seastar::connected_socket conn) {
    std::cout << "New connection" << std::endl;
    return seastar::make_ready_future<> ();
}

} // namespace stanxx

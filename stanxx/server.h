#pragma once

#include "transport.h"

#include <memory>
#include <seastar/core/app-template.hh>
#include <seastar/core/future.hh>
#include <seastar/net/api.hh>

namespace stanxx {

class Server {
    public:
    Server ();
    ~Server ();
    void run (int argc, char** argv);

    private:
    seastar::app_template app;

    seastar::future<> handle_client (seastar::connected_socket conn);
};
} // namespace stanxx
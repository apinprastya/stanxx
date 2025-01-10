
#include "server.h"

int main (int argc, char** argv) {
    stanxx::Server server;
    server.run (argc, argv);
    return 0;
}
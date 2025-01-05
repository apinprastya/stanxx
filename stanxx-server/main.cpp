
#include "server.h"
#include <iostream>

int main (int argc, char** argv) {
    stanxx::Server server;
    server.run (argc, argv);
    std::cout << "HOLA BOS" << std::endl;
    return 0;
}
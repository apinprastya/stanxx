#include "client.h"
#include "parser.h"
#include <benchmark/benchmark.h>
#include <fstream>
#include <iostream>

class DummyClient : public stanxx::Client {
    public:
    DummyClient () : stanxx::Client ("dummy", 0, nullptr, nullptr) {
    }

    void processConnect (std::span<const char> data) override {
    }
    void processPing () override {
    }
    void sendPing () override {
    }
    void sendError (const std::string& err) override {
    }
    void sendOK () override {
    }
    void processPong () override {
    }
    void processSubscribe (const std::vector<std::string_view>& args) override {
    }
    void processPublish (const stanxx::PublishArg& publishArg,
    std::span<const char> data) override {
    }
    void sendMessage (std::vector<char>&& data) override {
    }
};

std::vector<char> read_parser_debug (const std::string& filename) {
    std::ifstream file (filename, std::ios::binary);

    // Read header
    uint32_t data_size;
    file.read (reinterpret_cast<char*> (&data_size), sizeof (data_size));

    // Read data
    std::vector<char> data (data_size);
    file.read (data.data (), data_size);
    return data;
}

static void BM_ParseMessage (benchmark::State& state) {
    DummyClient client;
    stanxx::MessageParser parser (&client);
    std::vector<std::vector<char>> datas;
    for (int i = 0; i < 5; i++) {
        std::string filename = fmt::format ("parser_debug_{}.bin", i);
        std::ifstream file (filename, std::ios::binary);
        uint32_t data_size;
        file.read (reinterpret_cast<char*> (&data_size), sizeof (data_size));
        std::vector<char> data (data_size);
        file.read (data.data (), data_size);
        datas.push_back (data);
    }

    for (auto _ : state) {
        for (int i = 3; i < 4; i++) {
            // std::string message = "CONNECT arg\r\n";
            auto start = std::chrono::high_resolution_clock::now ();
            parser.parseMessage (std::span<char> (datas[i].data (), datas[i].size ()));
            auto end = std::chrono::high_resolution_clock::now ();
            auto duration =
            std::chrono::duration_cast<std::chrono::microseconds> (end - start);
            std::cout << "Execution time: " << duration.count () << "µs\n";
        }
    }
}

BENCHMARK (BM_ParseMessage)->Iterations (1);

BENCHMARK_MAIN ();

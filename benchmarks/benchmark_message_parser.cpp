#include "client.h"
#include "parser.h"
#include <array>
#include <benchmark/benchmark.h>
#include <fstream>
#include <iostream>
#include <map>
#include <seastar/core/temporary_buffer.hh>
#include <unordered_map>

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
    seastar::future<> sendMessage (seastar::temporary_buffer<char> data) override {
        return seastar::make_ready_future<> ();
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
            parser.parseMessage (
            seastar::temporary_buffer<char> (datas[i].data (), datas[i].size ()));
            auto end = std::chrono::high_resolution_clock::now ();
            auto duration =
            std::chrono::duration_cast<std::chrono::microseconds> (end - start);
            std::cout << "Execution time: " << duration.count () << "µs\n";
        }
    }
}

static void BM_AccessArrayByIndex (benchmark::State& state) {
    std::array<int, 1000> arr;
    for (int i = 0; i < 1000; ++i) {
        arr[i] = i;
    }

    for (auto _ : state) {
        benchmark::DoNotOptimize (arr[500]);
    }
}

static void BM_AccessMapByKey (benchmark::State& state) {
    std::map<int, int> m;
    for (int i = 0; i < 1000; ++i) {
        m[i] = i;
    }

    for (auto _ : state) {
        benchmark::DoNotOptimize (m[500]);
    }
}

static void BM_AccessUnorderedMapByKey (benchmark::State& state) {
    std::unordered_map<int, int> um;
    for (int i = 0; i < 1000; ++i) {
        um[i] = i;
    }

    for (auto _ : state) {
        benchmark::DoNotOptimize (um[500]);
    }
}

BENCHMARK (BM_ParseMessage)->Iterations (1);
BENCHMARK (BM_AccessArrayByIndex);
BENCHMARK (BM_AccessMapByKey);
BENCHMARK (BM_AccessUnorderedMapByKey);

BENCHMARK_MAIN ();

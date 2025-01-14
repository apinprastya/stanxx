#include "client.h"
#include "parser.h"
#include "gmock/gmock.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <seastar/core/app-template.hh>
#include <seastar/core/future.hh>
#include <seastar/core/temporary_buffer.hh>
#include <seastar/core/thread.hh>
#include <string_view>

using namespace stanxx;

class MockClient : public Client {
    public:
    // Constructor
    MockClient (const std::string& id, int cpuId, Connection* connection)
    : Client (id, cpuId, connection, nullptr) {
    }

    // Mock the virtual methods
    MOCK_METHOD (seastar::future<>, processError, (std::string_view errString), (override));
    MOCK_METHOD (seastar::future<>, processConnect, (std::span<const char> data), (override));
    MOCK_METHOD (seastar::future<>, processPing, (), (override));
    MOCK_METHOD (seastar::future<>, sendPing, (), (override));
    MOCK_METHOD (seastar::future<>, sendError, (const std::string& err), (override));
    MOCK_METHOD (seastar::future<>, sendOK, (), (override));
    MOCK_METHOD (seastar::future<>, processPong, (), (override));
    MOCK_METHOD (seastar::future<>,
    processSubscribe,
    (const std::vector<std::string_view>& args),
    (override));
    MOCK_METHOD (seastar::future<>,
    processPublish,
    (const PublishArg& publishArg, std::span<const char> data),
    (override));
    MOCK_METHOD (seastar::future<>, sendMessage, (seastar::temporary_buffer<char> data), (override));
};

class MessageParserTest : public ::testing::Test {
    protected:
    void SetUp () override {
        client = std::make_unique<MockClient> ("test", 0, nullptr);
        parser = std::make_unique<MessageParser> (client.get ());
    }

    std::unique_ptr<MockClient> client;
    std::unique_ptr<MessageParser> parser;
};

TEST_F (MessageParserTest, ParseConnectMessage) {
    seastar::temporary_buffer<char> data ("CONNECT arg\r\n", 13);

    ON_CALL (*client, processConnect (::testing::_)).WillByDefault ([] {
        return seastar::make_ready_future<> ();
    });

    for (int i = 1; i < data.size () - 1; i++) {
        EXPECT_CALL (*client, processConnect (::testing::_)).Times (testing::AtLeast (1));

        auto d1        = data.share (0, i);
        auto d2        = data.share (i, data.size () - i);
        auto result    = parser->parseMessage (d1.share ());
        auto resultGet = result.get ();
        EXPECT_FALSE (resultGet.has_value ());

        result    = parser->parseMessage (d2.share ());
        resultGet = result.get ();
        EXPECT_FALSE (resultGet.has_value ());
        if (resultGet.has_value ()) {
            std::cout << i << " : " << resultGet.value ().errorString () << std::endl;
        }
    }
}

TEST_F (MessageParserTest, ParsePingMessage) {
    seastar::temporary_buffer<char> data ("PING\n", 5);
    ON_CALL (*client, processPing ()).WillByDefault ([] {
        return seastar::make_ready_future<> ();
    });
    for (int i = 1; i < data.size () - 1; i++) {
        EXPECT_CALL (*client, processPing ()).Times (testing::AtLeast (1));
        auto d1        = data.share (0, i);
        auto d2        = data.share (i, data.size () - i);
        auto result    = parser->parseMessage (d1.share ());
        auto resultGet = result.get ();
        EXPECT_FALSE (resultGet.has_value ());

        result    = parser->parseMessage (d2.share ());
        resultGet = result.get ();
        EXPECT_FALSE (resultGet.has_value ());
        if (resultGet.has_value ()) {
            std::cout << i << " : " << resultGet.value ().errorString () << std::endl;
        }
    }
}

TEST_F (MessageParserTest, ParsePongMessage) {
    seastar::temporary_buffer<char> data ("PONG\n", 5);
    ON_CALL (*client, processPong ()).WillByDefault ([] {
        return seastar::make_ready_future<> ();
    });
    for (int i = 1; i < data.size () - 1; i++) {
        EXPECT_CALL (*client, processPong ()).Times (testing::AtLeast (1));
        auto d1        = data.share (0, i);
        auto d2        = data.share (i, data.size () - i);
        auto result    = parser->parseMessage (d1.share ());
        auto resultGet = result.get ();
        EXPECT_FALSE (resultGet.has_value ());

        result    = parser->parseMessage (d2.share ());
        resultGet = result.get ();
        EXPECT_FALSE (resultGet.has_value ());
        if (resultGet.has_value ()) {
            std::cout << i << " : " << resultGet.value ().errorString () << std::endl;
        }
    }
}

TEST_F (MessageParserTest, PaserPlusOKMessage) {
    seastar::temporary_buffer<char> data ("+OK\n", 4);

    for (int i = 1; i < data.size () - 1; i++) {
        auto d1        = data.share (0, i);
        auto d2        = data.share (i, data.size () - i);
        auto result    = parser->parseMessage (d1.share ());
        auto resultGet = result.get ();
        EXPECT_FALSE (resultGet.has_value ());

        result    = parser->parseMessage (d2.share ());
        resultGet = result.get ();
        EXPECT_FALSE (resultGet.has_value ());
        if (resultGet.has_value ()) {
            std::cout << i << " : " << resultGet.value ().errorString () << std::endl;
        }
    }
}

TEST_F (MessageParserTest, PaserMinusMessage) {
    std::string err = "this is the error";
    seastar::temporary_buffer<char> data ("-ERR\tthis is the error\n", 23);

    std::string value;
    ON_CALL (*client, processError (::testing::_)).WillByDefault ([&value] (std::string_view param) {
        value = param;
        return seastar::make_ready_future<> ();
    });
    for (int i = 1; i < data.size () - 1; i++) {
        EXPECT_CALL (*client, processError (::testing::_)).Times (testing::AtLeast (1));
        auto d1        = data.share (0, i);
        auto d2        = data.share (i, data.size () - i);
        auto result    = parser->parseMessage (d1.share ());
        auto resultGet = result.get ();
        EXPECT_FALSE (resultGet.has_value ());

        result    = parser->parseMessage (d2.share ());
        resultGet = result.get ();
        EXPECT_FALSE (resultGet.has_value ());
        if (resultGet.has_value ()) {
            std::cout << i << " : " << resultGet.value ().errorString () << std::endl;
        }
        EXPECT_EQ (value, err);
    }
}

TEST_F (MessageParserTest, ParsePublish) {
    seastar::temporary_buffer<char> data (
    "PUB hello.world 10\r\n0123456789\r\nPUB hello.world "
    "10\r\n0123456789\r\nPUB hello.world 10\r\n0123456789\r\nPUB hello.world "
    "10\r\n0123456789\r\n",
    128);
    ON_CALL (*client, processPublish (::testing::_, ::testing::_)).WillByDefault ([] {
        return seastar::make_ready_future<> ();
    });
    for (int i = 1; i < data.size () - 1; i++) {
        EXPECT_CALL (*client, processPublish (::testing::_, ::testing::_)).Times (testing::AnyNumber ());
        auto d1        = data.share (0, i);
        auto d2        = data.share (i, data.size () - i);
        auto result    = parser->parseMessage (d1.share ());
        auto resultGet = result.get ();
        EXPECT_FALSE (resultGet.has_value ());

        result    = parser->parseMessage (d2.share ());
        resultGet = result.get ();
        EXPECT_FALSE (resultGet.has_value ());
        if (resultGet.has_value ()) {
            std::cout << i << " : " << resultGet.value ().errorString () << std::endl;
        }
    }
}

int main (int argc, char** argv) {
    ::testing::InitGoogleTest (&argc, argv);
    seastar::app_template app;
    return app.run (
    argc, argv, [] { return seastar::async ([] { RUN_ALL_TESTS (); }); });
}
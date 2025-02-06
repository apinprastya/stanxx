#include <gmock/gmock.h>
#include <gtest/gtest.h>

// using namespace stanxx;

/*class MockClient : public Client {
    public:
    // Constructor
    MockClient (const std::string& id, int cpuId, Connection* connection, SubscriberManagerHandler* subscriberManagerHandler)
    : Client (id, cpuId, connection, subscriberManagerHandler) {
    }

    // Mock the virtual methods
    MOCK_METHOD (void, processConnect, (std::span<char> data), (override));
    MOCK_METHOD (void, processPing, (), (override));
    MOCK_METHOD (void, sendPing, (), (override));
    MOCK_METHOD (void, sendError, (const std::string& err), (override));
    MOCK_METHOD (void, sendOK, (), (override));
    MOCK_METHOD (void, processPong, (), (override));
    MOCK_METHOD (void, processSubscribe, (const std::vector<std::string_view>& args), (override));
    MOCK_METHOD (void,
    processPublish,
    (const PublishArg& publishArg, std::span<char> data),
    (override));
    MOCK_METHOD (void, sendMessage, (std::span<char> data), (override));
};

class MessageParserTest : public ::testing::Test {
    protected:
    void SetUp () override {
        client = std::make_unique<MockClient> ("test", 0, nullptr, nullptr);
        parser = std::make_unique<MessageParser> (client.get ());
    }

    std::unique_ptr<MockClient> client;
    std::unique_ptr<MessageParser> parser;
};

TEST_F (MessageParserTest, ParseConnectMessage) {
    seastar::temporary_buffer<char> data ("CONNECT arg\r\n", 13);

    for (int i = 1; i < data.size () - 1; i++) {
        EXPECT_CALL (*client, processConnect (::testing::_)).Times (testing::AtLeast (1));

        auto d1 = data.share (0, i);
        auto d2 = data.share (i, data.size () - i);
        auto result = parser->parseMessage (d1.share ());
        EXPECT_FALSE (result.has_value ());

        result = parser->parseMessage (d2.share ());
        EXPECT_FALSE (result.has_value ());
}
}

TEST_F (MessageParserTest, ParsePingMessage) {
    seastar::temporary_buffer<char> data ("PING\n", 5);

    EXPECT_CALL (*client, processPing ()).Times (testing::AtLeast (1));

    auto result = parser->parseMessage (std::move (data));
    EXPECT_FALSE (result.has_value ());
}
*/

int main (int argc, char** argv) {
    ::testing::InitGoogleTest (&argc, argv);
    return 0;
}
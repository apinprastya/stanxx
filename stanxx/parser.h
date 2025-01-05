#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace stanxx {

class Client;

enum class EParserState {
    OP_START,
    OP_PLUS,
    OP_PLUS_O,
    OP_PLUS_OK,
    OP_MINUS,
    OP_MINUS_E,
    OP_MINUS_ER,
    OP_MINUS_ERR,
    OP_MINUS_ERR_SPC,
    MINUS_ERR_ARG,
    OP_C,
    OP_CO,
    OP_CON,
    OP_CONN,
    OP_CONNE,
    OP_CONNEC,
    OP_CONNECT,
    CONNECT_ARG,
    OP_H,
    OP_HP,
    OP_HPU,
    OP_HPUB,
    OP_HPUB_SPC,
    HPUB_ARG,
    OP_HM,
    OP_HMS,
    OP_HMSG,
    OP_HMSG_SPC,
    HMSG_ARG,
    OP_P,
    OP_PU,
    OP_PUB,
    OP_PUB_SPC,
    PUB_ARG,
    OP_PI,
    OP_PIN,
    OP_PING,
    OP_PO,
    OP_PON,
    OP_PONG,
    MSG_PAYLOAD,
    MSG_END_R,
    MSG_END_N,
    OP_S,
    OP_SU,
    OP_SUB,
    OP_SUB_SPC,
    SUB_ARG,
    OP_A,
    OP_ASUB,
    OP_ASUB_SPC,
    ASUB_ARG,
    OP_AUSUB,
    OP_AUSUB_SPC,
    AUSUB_ARG,
    OP_L,
    OP_LS,
    OP_R,
    OP_RS,
    OP_U,
    OP_UN,
    OP_UNS,
    OP_UNSU,
    OP_UNSUB,
    OP_UNSUB_SPC,
    UNSUB_ARG,
    OP_M,
    OP_MS,
    OP_MSG,
    OP_MSG_SPC,
    MSG_ARG,
    OP_I,
    OP_IN,
    OP_INF,
    OP_INFO,
    INFO_ARG,
    OP_ERROR,
};

enum class ParseErrorCode {
    Err_None,
    Err_Parsing,
};

struct ParserError {
    ParseErrorCode code;
    std::string message;
    EParserState lastState;

    inline void setCodeAndError (ParseErrorCode code,
    EParserState lastState,
    const std::string& message) {
        this->code      = code;
        this->lastState = lastState;
        this->message   = message;
    }

    std::string errorString ();
};

struct ParserState {
    EParserState state = EParserState::OP_START;
    int drop{};
    int start{};
    std::vector<char> buff{};
    ParserError parserErr;

    inline void reset () {
        state = EParserState::OP_START;
        drop  = 0;
        start = 0;
        buff.clear ();
    }
};

struct PublishArg {
    std::string subject;
    std::string reply;
    int length;

    inline void reset () {
        subject = {};
        reply   = {};
        length  = {};
    }
};

class MessageParser {
    public:
    MessageParser (Client* client);
    std::optional<ParserError> parseMessage (const std::span<const char>& data);

    private:
    std::shared_ptr<Client> mClient;
    EParserState state = EParserState::OP_START;
    int mDrop{};
    int mStart{};
    std::optional<std::vector<char>> mBuff{};
    ParserError parserErr;
    PublishArg mPublishArg;

    void reset ();
    std::vector<std::string_view> splitSubcribeArg (const std::span<const char>& data);
    void parsePublishArg (const std::span<const char>& data);
};
} // namespace stanxx
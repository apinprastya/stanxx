#pragma once

#include <fmt/format.h>
#include <optional>
#include <seastar/core/future.hh>
#include <seastar/core/temporary_buffer.hh>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace stanxx {

class Client;

enum class EParserState : int {
    OP_START,
    OP_PLUS,
    OP_PLUS_OK,
    OP_MINUS,
    OP_MINUS_ERR_SPC,
    MINUS_ERR_ARG,
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
    OP_PING,
    OP_PO,
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
    OP_INFO,
    INFO_ARG,
    OP_ERROR,
    OP_NONE,
};
enum class EParserPlusState : int {
    OP_PLUS,
    OP_PLUS_O,
    OP_PLUS_OK,
};
enum class EParserMinusState : int {
    OP_MINUS,
    OP_MINUS_E,
    OP_MINUS_ER,
    OP_MINUS_ERR,
    OP_MINUS_ERR_SPC,
};
enum class EParserConnectState : int {
    OP_C,
    OP_CO,
    OP_CON,
    OP_CONN,
    OP_CONNE,
    OP_CONNEC,
    OP_CONNECT,
};
enum class EParserHPubState : int {
    OP_HP,
    OP_HPU,
    OP_HPUB,
    OP_HPUB_SPC,
};
enum class EParserPingState : int {
    OP_PI,
    OP_PIN,
    OP_PING,
};
enum class EParserPongState : int {
    OP_PO,
    OP_PON,
    OP_PONG,
};
enum class EParserPublishState : int {
    OP_PU,
    OP_PUB,
    OP_PUB_SPC,
};
enum class EParserInfoState : int {
    OP_I,
    OP_IN,
    OP_INF,
    OP_INFO,
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

template <typename TState> struct StateTransition {
    TState next_state;
    std::array<char, 2> expected_char;
    inline bool gotExpectedChar (char value) const {
        int i = 0;
        while (i < expected_char.size ()) {
            if (expected_char[i++] == value)
                return true;
        }
        return false;
    }
};


class MessageParser {
    public:
    MessageParser (Client* client);
    seastar::future<std::optional<ParserError>> parseMessage (
    seastar::temporary_buffer<char> data);

    private:
    Client* _client     = nullptr;
    EParserState _state = EParserState::OP_START;
    std::variant<EParserPlusState, EParserMinusState, EParserConnectState, EParserHPubState, EParserPublishState, EParserInfoState, EParserPingState, EParserPongState> _subState;
    int _subStateIdx{};
    int _drop{};
    int _start{};
    bool _buffAvailable{};
    std::vector<char> _buff{};
    ParserError _parserErr;
    PublishArg _publishArg;

    void reset ();
    std::vector<std::string_view> splitSubcribeArg (std::span<const char> data);
    void parsePublishArg (std::string_view data);

    template <typename TState, size_t N>
    int parseSubStateMachine (std::span<const char> data,
    int curIndex,
    const std::array<StateTransition<TState>, N>& transitions,
    TState finalState,
    EParserState stateAtFinal) {
        bool running = true;
        int j        = 0;
        auto* state  = &std::get<TState> (_subState);

        const char* p = data.data ();
        while (running && j < data.size ()) {
            const auto& transition = transitions[static_cast<int> (*state)];

            if (transition.gotExpectedChar (p[j])) {
                *state = transition.next_state;
                j++;
                if (*state == finalState) {
                    running = false;
                    _state  = stateAtFinal;
                    _start  = curIndex + j;
                    j--;
                    break;
                }
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, _state,
                fmt::format ("unable to parse sub state: {}", static_cast<int> (*state)));
                _state  = EParserState::OP_ERROR;
                running = false;
                break;
            }
        }
        return j;
    }
};
} // namespace stanxx
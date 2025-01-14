#include "parser.h"
#include "client.h"
#include <fmt/format.h>
#include <optional>
#include <seastar/core/coroutine.hh>
#include <seastar/core/seastar.hh>
#include <seastar/core/temporary_buffer.hh>
#include <span>
#include <spdlog/spdlog.h>
#include <string_view>

static constexpr size_t INITIAL_BUFFER_SIZE = 64 * 1024; // 64KB
constexpr const int lenCRLF                 = 2;

constexpr std::string_view to_string (stanxx::ParseErrorCode code) {
    switch (code) {
    case stanxx::ParseErrorCode::Err_None: return "none";
    case stanxx::ParseErrorCode::Err_Parsing: return "parsing";
    default: return "unknown";
    }
}

constexpr std::string_view to_string (stanxx::EParserState state) {
    using namespace stanxx;
    switch (state) {
    case EParserState::OP_START: return "OP_START";
    case EParserState::OP_PLUS: return "OP_PLUS";
    case EParserState::OP_MINUS: return "OP_MINUS";
    case EParserState::OP_MINUS_ERR_SPC: return "OP_MINUS_ERR_SPC";
    case EParserState::MINUS_ERR_ARG: return "MINUS_ERR_ARG";
    case EParserState::OP_CONNECT: return "OP_CONNECT";
    case EParserState::CONNECT_ARG: return "CONNECT_ARG";
    case EParserState::OP_H: return "OP_H";
    case EParserState::OP_HP: return "OP_HP";
    case EParserState::OP_HPU: return "OP_HPU";
    case EParserState::OP_HPUB: return "OP_HPUB";
    case EParserState::OP_HPUB_SPC: return "OP_HPUB_SPC";
    case EParserState::HPUB_ARG: return "HPUB_ARG";
    case EParserState::OP_HM: return "OP_HM";
    case EParserState::OP_HMS: return "OP_HMS";
    case EParserState::OP_HMSG: return "OP_HMSG";
    case EParserState::OP_HMSG_SPC: return "OP_HMSG_SPC";
    case EParserState::HMSG_ARG: return "HMSG_ARG";
    case EParserState::OP_P: return "OP_P";
    case EParserState::OP_PU: return "OP_PU";
    case EParserState::OP_PUB: return "OP_PUB";
    case EParserState::OP_PUB_SPC: return "OP_PUB_SPC";
    case EParserState::PUB_ARG: return "PUB_ARG";
    case EParserState::OP_PI: return "OP_PI";
    case EParserState::OP_PO: return "OP_PO";
    case EParserState::MSG_PAYLOAD: return "MSG_PAYLOAD";
    case EParserState::MSG_END_R: return "MSG_END_R";
    case EParserState::MSG_END_N: return "MSG_END_N";
    case EParserState::OP_S: return "OP_S";
    case EParserState::OP_SU: return "OP_SU";
    case EParserState::OP_SUB: return "OP_SUB";
    case EParserState::OP_SUB_SPC: return "OP_SUB_SPC";
    case EParserState::SUB_ARG: return "SUB_ARG";
    case EParserState::OP_A: return "OP_A";
    case EParserState::OP_ASUB: return "OP_ASUB";
    case EParserState::OP_ASUB_SPC: return "OP_ASUB_SPC";
    case EParserState::ASUB_ARG: return "ASUB_ARG";
    case EParserState::OP_AUSUB: return "OP_AUSUB";
    case EParserState::OP_AUSUB_SPC: return "OP_AUSUB_SPC";
    case EParserState::AUSUB_ARG: return "AUSUB_ARG";
    case EParserState::OP_L: return "OP_L";
    case EParserState::OP_LS: return "OP_LS";
    case EParserState::OP_R: return "OP_R";
    case EParserState::OP_RS: return "OP_RS";
    case EParserState::OP_U: return "OP_U";
    case EParserState::OP_UN: return "OP_UN";
    case EParserState::OP_UNS: return "OP_UNS";
    case EParserState::OP_UNSU: return "OP_UNSU";
    case EParserState::OP_UNSUB: return "OP_UNSUB";
    case EParserState::OP_UNSUB_SPC: return "OP_UNSUB_SPC";
    case EParserState::UNSUB_ARG: return "UNSUB_ARG";
    case EParserState::OP_M: return "OP_M";
    case EParserState::OP_MS: return "OP_MS";
    case EParserState::OP_MSG: return "OP_MSG";
    case EParserState::OP_MSG_SPC: return "OP_MSG_SPC";
    case EParserState::MSG_ARG: return "MSG_ARG";
    case EParserState::OP_INFO: return "OP_I";
    case EParserState::INFO_ARG: return "INFO_ARG";
    case EParserState::OP_ERROR: return "OP_ERROR";
    default: return "unknown";
    }
}

namespace stanxx {

inline std::string parserErrParsing = "error parsing message";

std::string ParserError::errorString () {
    return fmt::format ("code: {}: return "
                        ";lastState: {}: return "
                        ";message: {}",
    to_string (code), to_string (lastState), message);
}

static constexpr std::array<StateTransition<EParserPlusState>, 2> plusTransitions{ {
{ EParserPlusState::OP_PLUS_O, { 'o', 'O' } },
{ EParserPlusState::OP_PLUS_OK, { 'k', 'K' } },
} };

// EParserMinusState
static constexpr std::array<StateTransition<EParserMinusState>, 4> minusTransitions{ {
{ EParserMinusState::OP_MINUS_E, { 'e', 'E' } },
{ EParserMinusState::OP_MINUS_ER, { 'r', 'R' } },
{ EParserMinusState::OP_MINUS_ERR, { 'r', 'R' } },
{ EParserMinusState::OP_MINUS_ERR_SPC, { ' ', '\t' } },
} };

static constexpr std::array<StateTransition<EParserConnectState>, 7> connectTransitions{ {
{ EParserConnectState::OP_CO, { 'o', 'O' } },
{ EParserConnectState::OP_CON, { 'n', 'N' } },
{ EParserConnectState::OP_CONN, { 'n', 'N' } },
{ EParserConnectState::OP_CONNE, { 'e', 'E' } },
{ EParserConnectState::OP_CONNEC, { 'c', 'C' } },
{ EParserConnectState::OP_CONNECT, { 't', 'T' } },
} };

static constexpr std::array<StateTransition<EParserInfoState>, 7> infoTransations{ {
{ EParserInfoState::OP_IN, { 'n', 'N' } },
{ EParserInfoState::OP_INF, { 'f', 'F' } },
{ EParserInfoState::OP_INF, { 'o', 'O' } },
} };

static constexpr std::array<StateTransition<EParserPingState>, 3> pingTransations{ {
{ EParserPingState::OP_PIN, { 'n', 'N' } },
{ EParserPingState::OP_PING, { 'g', 'G' } },
} };

static constexpr std::array<StateTransition<EParserPongState>, 2> pongTransations{ {
{ EParserPongState::OP_PON, { 'n', 'N' } },
{ EParserPongState::OP_PONG, { 'g', 'G' } },
} };

MessageParser::MessageParser (Client* client) : _client (client) {
    _buff.reserve (INITIAL_BUFFER_SIZE);
}

void MessageParser::reset () {
    _state         = EParserState::OP_START;
    _drop          = 0;
    _start         = 0;
    _buffAvailable = false;
    _buff.clear ();
    _publishArg.reset ();
}

seastar::future<std::optional<ParserError>> MessageParser::parseMessage (
seastar::temporary_buffer<char> data) {
    for (int i = 0; i < data.size (); i++) {
        auto b = data[i];
        switch (_state) {
        case EParserState::OP_START: {
            switch (b) {
            case '+':
                _state    = EParserState::OP_PLUS;
                _subState = EParserPlusState::OP_PLUS;
                break;
            case '-':
                _state    = EParserState::OP_MINUS;
                _subState = EParserMinusState::OP_MINUS;
                break;
            case 'c':
            case 'C':
                _state    = EParserState::OP_CONNECT;
                _subState = EParserConnectState::OP_C;
                break;
            case 'h':
            case 'H': _state = EParserState::OP_H; break;
            case 'i':
            case 'I': _state = EParserState::OP_INFO; break;
            case 'p':
            case 'P': _state = EParserState::OP_P; break;
            case 's':
            case 'S': _state = EParserState::OP_S; break;
            case 'u':
            case 'U': _state = EParserState::OP_U; break;
            default:
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, _state, parserErrParsing);
                co_return _parserErr;
            }
        } break;
        case EParserState::OP_PLUS: {
            auto max = std::min (i + 3, static_cast<int> (data.size ()) - i);
            i += parseSubStateMachine (std::span<const char> (data.get () + i, max),
            i, plusTransitions, EParserPlusState::OP_PLUS_OK, EParserState::OP_PLUS_OK);
        } break;
        case EParserState::OP_PLUS_OK: {
            if (b == '\n') {
                reset ();
            }
        } break;
        case EParserState::OP_MINUS: {
            auto max = std::min (i + 5, static_cast<int> (data.size ()) - i);
            i += parseSubStateMachine (std::span<const char> (data.get () + i, max),
            i, minusTransitions, EParserMinusState::OP_MINUS_ERR_SPC,
            EParserState::OP_MINUS_ERR_SPC);
        } break;
        case EParserState::OP_MINUS_ERR_SPC: {
            if (b == ' ' || b == '\t') {
                continue;
            } else {
                _state = EParserState::MINUS_ERR_ARG;
                _start = i;
            }
        } break;
        case EParserState::MINUS_ERR_ARG: {
            const char* p   = data.get () + i;
            const char* end = data.get () + data.size ();

            while (p < end) {
                if (*p == '\r') {
                    _drop = 1;
                } else if (*p == '\n') {
                    const size_t length = (p - data.get ()) - _drop - _start;
                    std::string_view arg;

                    if (!_buffAvailable) {
                        arg = std::string_view (data.get () + _start, length);
                    } else {
                        auto oldSize = _buff.size ();
                        _buff.resize (oldSize + length);
                        std::memcpy (_buff.data () + oldSize, data.get (), length);
                        arg = std::string_view (_buff.data (), _buff.size ());
                    }
                    co_await _client->processError (arg);
                    if (!_buffAvailable) {
                        i = _start + length;
                    } else {
                        i = p - data.get ();
                    }
                    reset ();
                    break;
                }
                p++;
            }
        } break;
        case EParserState::OP_CONNECT: {
            // ONNECT is 6 chars
            auto max = std::min (i + 7, static_cast<int> (data.size ()) - i);
            i += parseSubStateMachine (std::span<const char> (data.get () + i, max),
            i, connectTransitions, EParserConnectState::OP_CONNECT,
            EParserState::CONNECT_ARG);
        } break;
        case EParserState::CONNECT_ARG:
            switch (b) {
            case '\r': _drop = 1; break;
            case '\n': {
                if (!_buffAvailable) {
                    auto length = i - _drop - _start;
                    co_await _client->processConnect (
                    std::span<const char> (data.begin () + _start, length));
                } else {
                    co_await _client->processConnect (_buff);
                }
                reset ();
            } break;
            default:
                if (_buffAvailable)
                    _buff.push_back (b);
                break;
            }
            break;
        case EParserState::OP_P:
            switch (b) {
            case 'i':
            case 'I':
                _state    = EParserState::OP_PI;
                _subState = EParserPingState::OP_PI;
                break;
            case 'o':
            case 'O':
                _state    = EParserState::OP_PO;
                _subState = EParserPongState::OP_PO;
                break;
            case 'u':
            case 'U':
                _state    = EParserState::OP_PU;
                _subState = EParserPublishState::OP_PU;
                break;
            default:
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, _state, parserErrParsing);
                _state = EParserState::OP_ERROR;
                break;
            }
            break;
        case EParserState::OP_PI: {
            // PING is 4 chars
            auto max = std::min (i + 4, static_cast<int> (data.size ()));
            i += parseSubStateMachine (std::span<const char> (data.get () + i, max),
            i, pingTransations, EParserPingState::OP_PING, EParserState::OP_PING);
        } break;
        case EParserState::OP_PING:
            if (b == '\n') {
                (void)_client->processPing ();
                reset ();
            }
            break;
        case EParserState::OP_PO: {
            // PONG is 4 chars
            auto max = std::min (i + 4, static_cast<int> (data.size ()));
            i += parseSubStateMachine (std::span<const char> (data.get () + i, max),
            i, pongTransations, EParserPongState::OP_PONG, EParserState::OP_PONG);
        } break;
        case EParserState::OP_PONG:
            if (b == '\n') {
                (void)_client->processPong ();
                reset ();
            }
            break;
        case EParserState::OP_S:
            if (b == 'u' || b == 'U') {
                _state = EParserState::OP_SU;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, _state, parserErrParsing);
                _state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_SU:
            if (b == 'b' || b == 'B') {
                _state = EParserState::OP_SUB;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, _state, parserErrParsing);
                _state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_SUB:
            if (b == ' ' || b == '\t') {
                _state = EParserState::OP_SUB_SPC;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, _state, parserErrParsing);
                _state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_SUB_SPC:
            if (b == ' ' || b == '\t') {
                continue;
            } else {
                _state = EParserState::SUB_ARG;
                _start = i;
            }
            break;
        case EParserState::SUB_ARG:
            switch (b) {
            case '\r': _drop = 1; break;
            case '\n': {
                std::span<const char> arg;
                if (!_buffAvailable) {
                    auto length = i - _drop - _start;
                    arg = std::span<const char> (data.begin () + _start, length);

                } else {
                    arg = _buff;
                }
                auto subscribeArgs = splitSubcribeArg (arg);
                co_await _client->processSubscribe (subscribeArgs);
                reset ();
            } break;
            default:
                if (_buffAvailable)
                    _buff.push_back (b);
                break;
            }
            break;
        case EParserState::OP_PU:
            if (b == 'b' || b == 'B') {
                _state = EParserState::OP_PUB;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, _state, parserErrParsing);
                _state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_PUB:
            if (b == ' ' || b == '\t') {
                _state = EParserState::OP_PUB_SPC;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, _state, parserErrParsing);
                _state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_PUB_SPC:
            if (b == ' ' || b == '\t') {
                continue;
            } else {
                _start = i;
                _state = EParserState::PUB_ARG;
            }
            break;
        case EParserState::PUB_ARG: {
            const char* p   = data.get () + i;
            const char* end = data.get () + data.size ();

            while (p < end) {
                if (*p == '\r') {
                    _drop = 1;
                } else if (*p == '\n') {
                    const size_t length = (p - data.get ()) - _drop - _start;
                    std::string_view arg;

                    if (!_buffAvailable) {
                        arg = std::string_view (data.get () + _start, length);
                    } else {
                        auto oldSize = _buff.size ();
                        _buff.resize (oldSize + length);
                        std::memcpy (_buff.data () + oldSize, data.get (), length);
                        arg = std::string_view (_buff.data (), _buff.size ());
                    }

                    parsePublishArg (arg);
                    _start = p - data.get () + 1;
                    _drop  = 0;
                    _state = EParserState::MSG_PAYLOAD;
                    _buff.clear ();
                    _buffAvailable = false;

                    if (!_buffAvailable) {
                        i = _start + _publishArg.length - lenCRLF;
                    } else {
                        i = p - data.get ();
                    }
                    break;
                }
                p++;
            }
        } break;
        case EParserState::MSG_PAYLOAD:
            if (_buffAvailable) {
                int sizeToCopy    = _publishArg.length - _buff.size ();
                int sizeAvailable = data.size () - i;
                if (sizeAvailable < sizeToCopy) {
                    sizeToCopy = sizeAvailable;
                }
                if (sizeToCopy > 0) {
                    auto oldSize = _buff.size ();
                    _buff.resize (oldSize + sizeToCopy);
                    std::memcpy (_buff.data () + oldSize, data.get () + i, sizeToCopy);
                    i = i + sizeToCopy - 1;
                }
                if (_buff.size () >= _publishArg.length) {
                    _state = EParserState::MSG_END_R;
                }
            } else if (i - _start + 1 >= _publishArg.length) {
                _state = EParserState::MSG_END_R;
            }
            break;
        case EParserState::MSG_END_R:
            if (b == '\r') {
                if (_buffAvailable) {
                    _buff.push_back (b);
                }
                _state = EParserState::MSG_END_N;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, _state, parserErrParsing);
                _state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::MSG_END_N:
            if (b != '\n') {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, _state, parserErrParsing);
                _state = EParserState::OP_ERROR;
                continue;
            }
            if (_buffAvailable) {
                _buff.push_back (b);
            } else {
                co_await _client->processPublish (_publishArg,
                std::span<const char> (_buff.data () + _start, _publishArg.length));
            }
            reset ();
            break;
        case stanxx::EParserState::OP_INFO: {
            // INFO is 4 chars
            auto max = std::min (i + 4, static_cast<int> (data.size ()));
            i += parseSubStateMachine (std::span<const char> (data.get () + i, max),
            i, infoTransations, EParserInfoState::OP_IN, EParserState::INFO_ARG);
        } break;

        case EParserState::OP_ERROR: {
            reset ();
            co_return _parserErr;
        }
        }
    }
    if (_state == EParserState::MINUS_ERR_ARG || _state == EParserState::CONNECT_ARG ||
    _state == EParserState::SUB_ARG || _state == EParserState::PUB_ARG) {
        if (!_buffAvailable) {
            _buffAvailable = true;
            _buff.resize (data.size () - _start);
            std::memcpy (_buff.data (), data.get () + _start, data.size () - _start);
        }
    }
    if (_state == EParserState::MSG_PAYLOAD) {
        if (!_buffAvailable) {
            _buffAvailable = true;
            _buff.resize (data.size () - _start);
            std::memcpy (_buff.data (), data.get () + _start, data.size () - _start);
        } else {
            auto oldSize = _buff.size ();
            _buff.resize (oldSize + (data.size () - _start));
            std::memcpy (_buff.data () + oldSize, data.get () + _start, data.size () - _start);
        }
    }
    _start = 0;
    _drop  = 0;
    co_return std::nullopt;
}

std::vector<std::string_view> MessageParser::splitSubcribeArg (std::span<const char> data) {
    std::vector<std::string_view> result;
    result.reserve (4);

    const char* start = data.data ();
    const char* end   = start + data.size ();
    const char* p     = start;
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
            p++;
        }
        if (p >= end)
            break;
        const char* token_start = p;
        while (p < end && !(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
            p++;
        }
        if (token_start < p) {
            result.push_back (std::string_view (token_start, p - token_start));
        }
    }
    return result;
}

void MessageParser::parsePublishArg (std::string_view data) {
    size_t pos   = 0;
    size_t space = data.find (' ');
    if (space == std::string_view::npos)
        return;

    _publishArg.subject = std::string (data.substr (0, space));
    pos                 = data.find_first_not_of (' ', space);
    if (pos == std::string_view::npos)
        return;

    space = data.find (' ', pos);
    if (space == std::string_view::npos) {
        auto len_str       = data.substr (pos);
        _publishArg.length = std::stoul (std::string (len_str));
    } else {
        _publishArg.reply = std::string (data.substr (pos, space - pos));
        pos               = data.find_first_not_of (' ', space);
        if (pos == std::string_view::npos)
            return;

        auto len_str       = data.substr (pos);
        _publishArg.length = std::stoul (std::string (len_str));
    }
}

} // namespace stanxx
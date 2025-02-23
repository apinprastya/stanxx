#include "parser.h"
#include "client.h"
#include <charconv>
#include <cstring>
#include <fmt/format.h>
#include <optional>
#include <span>
#include <spdlog/spdlog.h>
#include <string_view>
#include <system_error>

constexpr const int lenCRLF = 2;

using microseconds = std::chrono::duration<int64_t, std::micro>;

std::string quote2 (const std::string& str) {
    std::ostringstream oss;
    oss << '"';
    for (char c : str) {
        switch (c) {
        case '\\': oss << "\\\\"; break;
        case '"': oss << "\\\""; break;
        case '\n': oss << "\\n"; break;
        case '\r': oss << "\\r"; break;
        case '\t': oss << "\\t"; break;
        default:
            if (std::isprint (static_cast<unsigned char> (c))) {
                oss << c;
            } else {
                oss << "\\x" << std::hex << std::setw (2) << std::setfill ('0')
                    << (static_cast<int> (c) & 0xFF);
            }
            break;
        }
    }
    oss << '"';
    return oss.str ();
}


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
    case EParserState::OP_PLUS_O: return "OP_PLUS_O";
    case EParserState::OP_PLUS_OK: return "OP_PLUS_OK";
    case EParserState::OP_MINUS: return "OP_MINUS";
    case EParserState::OP_MINUS_E: return "OP_MINUS_E";
    case EParserState::OP_MINUS_ER: return "OP_MINUS_ER";
    case EParserState::OP_MINUS_ERR: return "OP_MINUS_ERR";
    case EParserState::OP_MINUS_ERR_SPC: return "OP_MINUS_ERR_SPC";
    case EParserState::MINUS_ERR_ARG: return "MINUS_ERR_ARG";
    case EParserState::OP_C: return "OP_C";
    case EParserState::OP_CO: return "OP_CO";
    case EParserState::OP_CON: return "OP_CON";
    case EParserState::OP_CONN: return "OP_CONN";
    case EParserState::OP_CONNE: return "OP_CONNE";
    case EParserState::OP_CONNEC: return "OP_CONNEC";
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
    case EParserState::OP_PIN: return "OP_PIN";
    case EParserState::OP_PING: return "OP_PING";
    case EParserState::OP_PO: return "OP_PO";
    case EParserState::OP_PON: return "OP_PON";
    case EParserState::OP_PONG: return "OP_PONG";
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
    case EParserState::OP_I: return "OP_I";
    case EParserState::OP_IN: return "OP_IN";
    case EParserState::OP_INF: return "OP_INF";
    case EParserState::OP_INFO: return "OP_INFO";
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

MessageParser::MessageParser (Client* client) : _client (client) {
    _buff.reserve (INITIAL_BUFFER_SIZE);
}

void MessageParser::reset () {
    state  = EParserState::OP_START;
    _drop  = 0;
    _start = 0;
    _buff.clear ();
    _buffAvailable = false;
    _publishArg.reset ();
}

std::optional<ParserError> MessageParser::parseMessage (const std::span<const char>& data) {
    spdlog::info ("new data length: {}", data.size ());

    microseconds durationPublish{};
    microseconds processPublishDuration{};
    auto publishStart = std::chrono::high_resolution_clock::now ();

    for (int i = 0; i < data.size (); i++) {
        auto b = data[i];
        switch (state) {
        case EParserState::OP_START: {
            switch (b) {
            case 'C':
            case 'c': state = EParserState::OP_C; break;
            case 'P':
            case 'p': state = EParserState::OP_P; break;
            case 'S':
            case 's': state = EParserState::OP_S; break;
            default:
                state = EParserState::OP_ERROR;
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                break;
            }
        } break;
        case EParserState::OP_C: {
            if (b == 'o' || b == 'O') {
                state = EParserState::OP_CO;
                break;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
        } break;
        case EParserState::OP_CO:
            if (b == 'n' || b == 'N') {
                state = EParserState::OP_CON;
                break;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_CON:
            if (b == 'n' || b == 'N') {
                state = EParserState::OP_CONN;
                break;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_CONN:
            if (b == 'e' || b == 'E') {
                state = EParserState::OP_CONNE;
                break;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_CONNE:
            if (b == 'c' || b == 'C') {
                state = EParserState::OP_CONNEC;
                break;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_CONNEC:
            if (b == 't' || b == 'T') {
                state = EParserState::OP_CONNECT;
                break;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_CONNECT:
            if (b == ' ' || b == '\t') {
                state  = EParserState::CONNECT_ARG;
                _start = i;
            }
            break;
        case EParserState::CONNECT_ARG:
            switch (b) {
            case '\r': _drop = 1; break;
            case '\n': {
                if (!_buffAvailable) {
                    _buffAvailable = true;
                    auto length    = i - _drop - _start;
                    _client->processConnect (
                    std::span<const char> (data.data () + _start, length));
                } else {
                    _client->processConnect (_buff);
                }
                reset ();
            } break;
            default:
                if (_buffAvailable)
                    _buff.push_back (b);
                break;
            }
            break;
        case EParserState::OP_P: {
            switch (b) {
            case 'i':
            case 'I': state = EParserState::OP_PI; break;
            case 'o':
            case 'O': state = EParserState::OP_PO; break;
            case 'u':
            case 'U': state = EParserState::OP_PU; break;
            default:
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
                break;
            }
        } break;
        case EParserState::OP_PI:
            if (b == 'n' || b == 'N') {
                state = EParserState::OP_PIN;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_PIN:
            if (b == 'g' || b == 'G') {
                state = EParserState::OP_PING;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_PING:
            if (b == '\n') {
                _client->processPing ();
                reset ();
            }
            break;
        case EParserState::OP_PO:
            if (b == 'n' || b == 'N') {
                state = EParserState::OP_PON;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_PON:
            if (b == 'g' || b == 'G') {
                state = EParserState::OP_PONG;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_PONG:
            if (b == '\n') {
                _client->processPong ();
                reset ();
            }
            break;
        case EParserState::OP_S:
            if (b == 'u' || b == 'U') {
                state = EParserState::OP_SU;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_SU:
            if (b == 'b' || b == 'B') {
                state = EParserState::OP_SUB;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_SUB:
            if (b == ' ' || b == '\t') {
                state = EParserState::OP_SUB_SPC;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_SUB_SPC:
            if (b == ' ' || b == '\t') {
                continue;
            } else {
                state  = EParserState::SUB_ARG;
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
                    arg = std::span<const char> (data.data () + _start, length);
                } else {
                    arg = _buff;
                }
                auto subscribeArgs = splitSubcribeArg (arg);
                _client->processSubscribe (subscribeArgs);
                reset ();
            } break;
            default:
                if (_buffAvailable) {
                    auto oldSize = _buff.size ();
                    _buff.resize (oldSize + 1);
                    std::memcpy (_buff.data () + oldSize, &b, 1);
                }
                break;
            }
            break;
        case EParserState::OP_PU:
            if (b == 'b' || b == 'B') {
                state = EParserState::OP_PUB;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_PUB:
            if (b == ' ' || b == '\t') {
                state = EParserState::OP_PUB_SPC;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_PUB_SPC:
            if (b == ' ' || b == '\t') {
                continue;
            } else {
                _start = i;
                state  = EParserState::PUB_ARG;
            }
            break;
        case EParserState::PUB_ARG:
            switch (b) {
            case '\r': _drop = 1; break;
            case '\n': {
                std::span<const char> arg;
                if (!_buffAvailable) {
                    auto length = i - _drop - _start;
                    arg = std::span<const char> (data.data () + _start, length);
                } else {
                    arg = _buff;
                }
                publishStart = std::chrono::high_resolution_clock::now ();
                parsePublishArg (std::string_view (arg.data (), arg.size ()));
                _start = i + 1;
                _drop  = 0;
                state  = EParserState::MSG_PAYLOAD;
                _buff.clear ();
                _buffAvailable = false;
                /*if (!_buff) {
                    i = _start + _publishArg.length - lenCRLF;
                }*/
            } break;
            default:
                if (_buffAvailable) {
                    auto oldSize = _buff.size ();
                    _buff.resize (oldSize + 1);
                    std::memcpy (_buff.data () + oldSize, &b, 1);
                }
                break;
            }
            break;
        case EParserState::MSG_PAYLOAD:
            if (_buffAvailable) {
                auto s            = std::chrono::high_resolution_clock::now ();
                int sizeToCopy    = _publishArg.length - _buff.size ();
                int sizeAvailable = data.size () - i;
                if (sizeAvailable < sizeToCopy) {
                    sizeToCopy = sizeAvailable;
                }
                if (sizeToCopy > 0) {
                    auto s       = std::chrono::high_resolution_clock::now ();
                    auto oldSize = _buff.size ();
                    _buff.resize (oldSize + sizeToCopy);
                    std::memcpy (_buff.data () + oldSize, data.data () + i, sizeToCopy);
                    i        = i + sizeToCopy - 1;
                    auto end = std::chrono::high_resolution_clock::now ();
                    processPublishDuration +=
                    std::chrono::duration_cast<std::chrono::microseconds> (end - s);
                }
                if (_buff.size () >= _publishArg.length) {
                    // publishStart = std::chrono::high_resolution_clock::now ();
                    state = EParserState::MSG_END_R;
                }
                auto end = std::chrono::high_resolution_clock::now ();
                processPublishDuration +=
                std::chrono::duration_cast<std::chrono::microseconds> (end - s);
            } else if (i - _start + 1 >= _publishArg.length) {
                // publishStart = std::chrono::high_resolution_clock::now ();
                state = EParserState::MSG_END_R;
            }
            // publishStart = std::chrono::high_resolution_clock::now ();
            break;
        case EParserState::MSG_END_R:
            if (b == '\r') {
                if (_buffAvailable) {
                    auto oldSize = _buff.size ();
                    _buff.resize (oldSize + 1);
                    std::memcpy (_buff.data () + oldSize, &b, 1);
                }
                state = EParserState::MSG_END_N;
            } else {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::MSG_END_N: {
            if (b != '\n') {
                _parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
                continue;
            }
            if (_buffAvailable) {
                auto oldSize = _buff.size ();
                _buff.resize (oldSize + 1);
                std::memcpy (_buff.data () + oldSize, &b, 1);
            } else {
                auto s       = std::chrono::high_resolution_clock::now ();
                auto subSpan = data.subspan (_start, _publishArg.length);
                _buff    = std::vector<char> (subSpan.begin (), subSpan.end ());
                auto end = std::chrono::high_resolution_clock::now ();
                processPublishDuration +=
                std::chrono::duration_cast<std::chrono::microseconds> (end - s);
                /*spdlog::debug (
                "real data: {}", std::string (_buff->begin (), _buff->end ()));*/
            }
            _client->processPublish (
            _publishArg, std::span<char> (_buff.begin (), _buff.end ()));
            reset ();
            auto end = std::chrono::high_resolution_clock::now ();
            durationPublish +=
            std::chrono::duration_cast<std::chrono::microseconds> (end - publishStart);

        } break;

        case EParserState::OP_ERROR: reset (); return _parserErr;
        }
    }

    if (state == EParserState::CONNECT_ARG || state == EParserState::SUB_ARG ||
    state == EParserState::PUB_ARG) {
        if (!_buffAvailable) {
            _buffAvailable = true;
            _buff.resize (data.size () - _drop);
            std::memcpy (_buff.data (), data.data () + _start, data.size () - _drop);
        }
    }
    if (state == EParserState::MSG_PAYLOAD) {
        if (!_buffAvailable) {
            _buffAvailable = true;
            _buff.resize (data.size () - _start);
            std::memcpy (_buff.data (), data.data () + _start, data.size () - _start);
        } else {
            auto oldSize = _buff.size ();
            _buff.resize (oldSize + (data.size () - _start));
            std::memcpy (_buff.data () + oldSize, data.data () + _start,
            data.size () - _start);
        }
    }

    std::cout << "parse duration: " << durationPublish.count ()
              << " process publish: " << processPublishDuration.count ()
              << " length: " << data.size () << "\n";

    return std::nullopt;
}

std::vector<std::string_view> MessageParser::splitSubcribeArg (
const std::span<const char>& data) {
    std::vector<std::string_view> result;
    int start = -1;
    for (int i = 0; i < data.size (); i++) {
        auto b = data[i];
        switch (b) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
            if (start >= 0) {
                result.push_back (std::string_view (data.data () + start, i - start));
                start = -1;
            }
            break;
        default:
            if (start < 0) {
                start = i;
            }
        }
    }
    if (start >= 0) {
        result.push_back (std::string_view (data.data () + start, data.size () - start));
    }
    return result;
}

void MessageParser::parsePublishArg (std::string_view data) {
    /*std::vector<std::string_view> splits;
    int start = -1;
    for (int i = 0; i < data.size (); i++) {
        auto b = data[i];
        switch (b) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
            if (start >= 0) {
                splits.push_back (std::string_view (data.data () + start, i - start));
                start = -1;
            }
            break;
        default:
            if (start < 0) {
                start = i;
            }
        }
    }
    if (start >= 0) {
        splits.push_back (std::string_view (data.data () + start, data.size () - start));
    }
    int value;
    if (splits.size () == 2) {
        _publishArg.subject = std::string (splits[0]);
        auto [ptr, ec]      = std::from_chars (
        splits[1].data (), splits[1].data () + splits[1].size (), value);
        if (ec == std::errc ()) {
            _publishArg.length = value;
        } else {
            spdlog::error ("parsePublishArg with length 2: got error {}",
            std::string (data.begin (), data.end ()));
        }
    } else if (splits.size () == 3) {
        _publishArg.subject = std::string (splits[0]);
        _publishArg.reply   = std::string (splits[1]);
        auto [ptr, ec]      = std::from_chars (
        splits[2].data (), splits[2].data () + splits[2].size (), value);
        if (ec == std::errc ()) {
            _publishArg.length = value;
        } else {
            spdlog::error ("parsePublishArg with length 3: got error {}",
            std::string (data.begin (), data.end ()));
        }
    }*/
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
        // 2-part format: subject length
        auto len_str       = data.substr (pos);
        _publishArg.length = std::stoul (std::string (len_str));
    } else {
        // 3-part format: subject reply length
        _publishArg.reply = std::string (data.substr (pos, space - pos));
        pos               = data.find_first_not_of (' ', space);
        if (pos == std::string_view::npos)
            return;

        auto len_str       = data.substr (pos);
        _publishArg.length = std::stoul (std::string (len_str));
    }
}

} // namespace stanxx
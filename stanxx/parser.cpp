#include "parser.h"
#include "client.h"
#include <charconv>
#include <fmt/format.h>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <spdlog/spdlog.h>
#include <string_view>
#include <system_error>

constexpr const int lenCRLF = 2;

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

MessageParser::MessageParser (Client* client) : mClient (client) {
}

void MessageParser::reset () {
    state  = EParserState::OP_START;
    mDrop  = 0;
    mStart = 0;
    if (mBuff)
        mBuff.value ().clear ();
    mBuff = std::nullopt;
    mPublishArg.reset ();
}

std::optional<ParserError> MessageParser::parseMessage (const std::span<const char>& data) {
    for (int i; i < data.size (); i++) {
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
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                break;
            }
        } break;
        case EParserState::OP_C: {
            if (b == 'o' || b == 'O') {
                state = EParserState::OP_CO;
                break;
            } else {
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
        } break;
        case EParserState::OP_CO:
            if (b == 'n' || b == 'N') {
                state = EParserState::OP_CON;
                break;
            } else {
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_CON:
            if (b == 'n' || b == 'N') {
                state = EParserState::OP_CONN;
                break;
            } else {
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_CONN:
            if (b == 'e' || b == 'E') {
                state = EParserState::OP_CONNE;
                break;
            } else {
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_CONNE:
            if (b == 'c' || b == 'C') {
                state = EParserState::OP_CONNEC;
                break;
            } else {
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_CONNEC:
            if (b == 't' || b == 'T') {
                state = EParserState::OP_CONNECT;
                break;
            } else {
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_CONNECT:
            if (b == ' ' || b == '\t') {
                state  = EParserState::CONNECT_ARG;
                mStart = i;
            }
            break;
        case EParserState::CONNECT_ARG:
            switch (b) {
            case '\r': mDrop = i; break;
            case '\n': {
                std::span<const char> arg;
                if (!mBuff) {
                    auto length = mDrop - mStart;
                    arg = std::span<const char> (data.data () + mStart, length);
                } else {
                    arg = mBuff.value ();
                }
                // mClient->processConnect (arg);
                reset ();
            } break;
            default:
                if (mBuff)
                    mBuff.value ().push_back (b);
                break;
            }
            break;
        case EParserState::OP_P:
            switch (b) {
            case 'i':
            case 'I': state = EParserState::OP_PI; break;
            case 'o':
            case 'O': state = EParserState::OP_PO; break;
            case 'u':
            case 'U': state = EParserState::OP_PU; break;
            default:
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
                break;
            }
            break;
        case EParserState::OP_PI:
            if (b == 'n' || b == 'N') {
                state = EParserState::OP_PIN;
            } else {
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_PIN:
            if (b == 'g' || b == 'G') {
                state = EParserState::OP_PING;
            } else {
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_PING:
            if (b == '\n') {
                // mClient->processPing ();
                reset ();
            }
            break;
        case EParserState::OP_PO:
            if (b == 'n' || b == 'N') {
                state = EParserState::OP_PON;
            } else {
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_PON:
            if (b == 'g' || b == 'G') {
                state = EParserState::OP_PONG;
            } else {
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_PONG:
            if (b == '\n') {
                // mClient->processPong ();
                reset ();
            }
            break;
        case EParserState::OP_S:
            if (b == 'u' || b == 'U') {
                state = EParserState::OP_SU;
            } else {
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_SU:
            if (b == 'b' || b == 'B') {
                state = EParserState::OP_SUB;
            } else {
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_SUB:
            if (b == ' ' || b == '\t') {
                state = EParserState::OP_SUB_SPC;
            } else {
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_SUB_SPC:
            if (b == ' ' || b == '\t') {
                continue;
            } else {
                state  = EParserState::SUB_ARG;
                mStart = i;
            }
            break;
        case EParserState::SUB_ARG:
            switch (b) {
            case '\r': mDrop = i; break;
            case '\n': {
                std::span<const char> arg;
                if (!mBuff) {
                    auto length = mDrop - mStart;
                    arg = std::span<const char> (data.data () + mStart, length);
                } else {
                    arg = mBuff.value ();
                }
                auto subscribeArgs = splitSubcribeArg (arg);
                // mClient->processSubscribe (subscribeArgs);
                reset ();
            } break;
            default:
                if (mBuff)
                    mBuff.value ().push_back (b);
                break;
            }
            break;
        case EParserState::OP_PU:
            if (b == 'b' || b == 'B') {
                state = EParserState::OP_PUB;
            } else {
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_PUB:
            if (b == ' ' || b == '\t') {
                state = EParserState::OP_PUB_SPC;
            } else {
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::OP_PUB_SPC:
            if (b == ' ' || b == '\t') {
                continue;
            } else {
                mStart = i;
                state  = EParserState::PUB_ARG;
            }
            break;
        case EParserState::PUB_ARG:
            switch (b) {
            case '\r': mDrop = i; break;
            case '\n': {
                std::span<const char> arg;
                if (!mBuff) {
                    auto length = mDrop - mStart;
                    arg = std::span<const char> (data.data () + mStart, length);
                } else {
                    arg = mBuff.value ();
                }
                parsePublishArg (arg);
                mStart = i + 1;
                mDrop  = 0;
                state  = EParserState::MSG_PAYLOAD;
                if (!mBuff) {
                    i = mStart + mPublishArg.length - lenCRLF;
                }
            } break;
            default:
                if (mBuff)
                    mBuff.value ().push_back (b);
                break;
            }
            break;
        case EParserState::MSG_PAYLOAD:
            if (mBuff) {

            } else if (i - mStart + 1 >= mPublishArg.length) {
                state = EParserState::MSG_END_R;
            }
            break;
        case EParserState::MSG_END_R:
            if (b == '\r') {
                if (mBuff) {
                    mBuff.value ().push_back (b);
                } else {
                }
                state = EParserState::MSG_END_N;
            } else {
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
            }
            break;
        case EParserState::MSG_END_N:
            if (b != '\n') {
                parserErr.setCodeAndError (ParseErrorCode::Err_Parsing, state, parserErrParsing);
                state = EParserState::OP_ERROR;
                continue;
            }
            if (mBuff) {
                mBuff.value ().push_back (b);
            } else {
                auto subSpan = data.subspan (mStart, mPublishArg.length);
                mBuff = std::vector<char> (subSpan.begin (), subSpan.end ());
                spdlog::debug (
                "real data: {}", std::string (mBuff->begin (), mBuff->end ()));
            }
            /*mClient->processPublish (
            mPublishArg, std::span<char> (mBuff->begin (), mBuff->end ()));*/
            reset ();
            break;

        case EParserState::OP_ERROR: reset (); return parserErr;
        }
    }
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

void MessageParser::parsePublishArg (const std::span<const char>& data) {
    std::vector<std::string_view> splits;
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
        mPublishArg.subject = std::string (splits[0]);
        auto [ptr, ec]      = std::from_chars (
             splits[1].data (), splits[1].data () + splits[1].size (), value);
        if (ec == std::errc ()) {
            mPublishArg.length = value;
        } else {
            spdlog::error ("parsePublishArg with length 2: got error {}",
            std::string (data.begin (), data.end ()));
        }
    } else if (splits.size () == 3) {
        mPublishArg.subject = std::string (splits[0]);
        mPublishArg.reply   = std::string (splits[1]);
        auto [ptr, ec]      = std::from_chars (
             splits[2].data (), splits[2].data () + splits[2].size (), value);
        if (ec == std::errc ()) {
            mPublishArg.length = value;
        } else {
            spdlog::error ("parsePublishArg with length 3: got error {}",
            std::string (data.begin (), data.end ()));
        }
    }
}

} // namespace stanxx
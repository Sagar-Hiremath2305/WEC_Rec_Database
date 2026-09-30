#include "resp.h"
#include <sstream>

RespParser::RespParser() {
    reset();
}

void RespParser::reset() {
    state = RespParserState::START;
    current_command.clear();
    current_token.clear();
    error_msg.clear();
    expected_elements = 0;
    current_element_len = 0;
    bulk_bytes_read = 0;
}

bool RespParser::has_error() const {
    return state == RespParserState::ERROR;
}

std::string RespParser::get_error() const {
    return error_msg;
}

void RespParser::set_error(const std::string& msg) {
    state = RespParserState::ERROR;
    error_msg = msg;
}

std::vector<std::string> RespParser::get_command() {
    std::vector<std::string> cmd = std::move(current_command);
    reset();
    return cmd;
}

bool RespParser::feed(char c) {
    if (state == RespParserState::ERROR) return false;

    switch (state) {
        case RespParserState::START:
            if (c == '*') {
                state = RespParserState::ARRAY_LEN;
                current_token.clear();
            } else {
                state = RespParserState::INLINE_CHAR;
                current_token.clear();
                current_token += c;
            }
            break;
            
        case RespParserState::INLINE_CHAR:
            if (c == '\r') {
                state = RespParserState::INLINE_CR;
            } else if (c == '\n') { // Some clients send only \n
                parse_inline_tokens();
                if (state != RespParserState::ERROR) {
                    return true;
                }
            } else {
                current_token += c;
                if (current_token.size() > MAX_INLINE_SIZE) {
                    set_error("Protocol error: inline command too long");
                }
            }
            break;
            
        case RespParserState::INLINE_CR:
            if (c == '\n') {
                parse_inline_tokens();
                if (state != RespParserState::ERROR) {
                    return true;
                }
            } else {
                set_error("Protocol error: expected LF after CR in inline command");
            }
            break;

        case RespParserState::ARRAY_LEN:
            if (c == '\r') {
                state = RespParserState::ARRAY_LEN_CR;
            } else if (c >= '0' && c <= '9') {
                current_token += c;
            } else {
                set_error("Protocol error: invalid character in array length");
            }
            break;

        case RespParserState::ARRAY_LEN_CR:
            if (c == '\n') {
                if (current_token.empty()) {
                    set_error("Protocol error: empty array length");
                    break;
                }
                try {
                    expected_elements = std::stoll(current_token);
                    if (expected_elements <= 0) {
                        set_error("Protocol error: invalid array length");
                        break;
                    }
                    current_command.reserve(expected_elements);
                    state = RespParserState::BULK_LEN;
                    current_token.clear();
                } catch (...) {
                    set_error("Protocol error: array length too large");
                }
            } else {
                set_error("Protocol error: expected LF after CR");
            }
            break;

        case RespParserState::BULK_LEN:
            if (current_token.empty() && c == '$') {
                // skip '$'
            } else if (c == '\r') {
                state = RespParserState::BULK_LEN_CR;
            } else if (c >= '0' && c <= '9') {
                current_token += c;
            } else {
                set_error("Protocol error: invalid character in bulk length");
            }
            break;

        case RespParserState::BULK_LEN_CR:
            if (c == '\n') {
                try {
                    current_element_len = std::stoll(current_token);
                    if (current_element_len < 0 || current_element_len >= MAX_BULK_SIZE) {
                        set_error("Protocol error: invalid bulk length");
                        break;
                    }
                    current_token.clear();
                    current_token.reserve(current_element_len);
                    bulk_bytes_read = 0;
                    if (current_element_len == 0) {
                        state = RespParserState::BULK_CR;
                    } else {
                        state = RespParserState::BULK_DATA;
                    }
                } catch (...) {
                    set_error("Protocol error: bulk length too large");
                }
            } else {
                set_error("Protocol error: expected LF after CR");
            }
            break;

        case RespParserState::BULK_DATA:
            current_token += c;
            bulk_bytes_read++;
            if (bulk_bytes_read == (size_t)current_element_len) {
                state = RespParserState::BULK_CR;
            }
            break;

        case RespParserState::BULK_CR:
            if (c == '\r') {
                // wait for LF
            } else if (c == '\n') {
                current_command.push_back(std::move(current_token));
                current_token.clear();
                if (current_command.size() == (size_t)expected_elements) {
                    return true;
                } else {
                    state = RespParserState::BULK_LEN;
                }
            } else {
                set_error("Protocol error: expected CRLF after bulk data");
            }
            break;

        case RespParserState::ERROR:
            break;
    }
    return false;
}

bool RespParser::feed(const char* data, size_t len, size_t& consumed) {
    consumed = 0;
    while (consumed < len) {
        if (feed(data[consumed++])) {
            return true;
        }
        if (has_error()) {
            return false;
        }
    }
    return false;
}

void RespParser::parse_inline_tokens() {
    std::stringstream ss(current_token);
    std::string token;
    while (ss >> token) {
        current_command.push_back(token);
    }
    if (current_command.empty()) {
        set_error("Protocol error: empty inline command");
    }
}

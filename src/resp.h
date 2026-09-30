#pragma once

#include <string>
#include <vector>
#include <cstdint>

enum class RespParserState {
    START,
    INLINE_CHAR,
    INLINE_CR,
    ARRAY_LEN,
    ARRAY_LEN_CR,
    BULK_LEN,
    BULK_LEN_CR,
    BULK_DATA,
    BULK_CR,
    ERROR
};

class RespParser {
public:
    RespParser();

    // Feed a single byte to the parser.
    // Returns true if a complete command has been parsed.
    bool feed(char c);

    // Feed multiple bytes. Returns true if a command is ready.
    bool feed(const char* data, size_t len, size_t& consumed);

    std::vector<std::string> get_command();
    bool has_error() const;
    std::string get_error() const;
    void reset();

private:
    RespParserState state;
    std::vector<std::string> current_command;
    std::string current_token;
    std::string error_msg;

    long long expected_elements;
    long long current_element_len;
    size_t bulk_bytes_read;

    static constexpr long long MAX_BULK_SIZE = 512 * 1024 * 1024; // 512MB
    static constexpr long long MAX_INLINE_SIZE = 64 * 1024;

    void set_error(const std::string& msg);
    void parse_inline_tokens();
};

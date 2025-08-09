#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <streambuf>
#include <string>

class spdlog_streambuf : public std::streambuf {
public:
    explicit spdlog_streambuf(std::shared_ptr<spdlog::logger> logger)
        : _logger(std::move(logger)) {
    }

protected:
    int_type overflow(int_type ch) override {
        if (traits_type::eq_int_type(ch, traits_type::eof()))
            return ch;
        char c = traits_type::to_char_type(ch);
        _buffer += c;
        if (c == '\n') {
            _logger->info(_buffer);
            _buffer.clear();
        }
        return ch;
    }

private:
    std::shared_ptr<spdlog::logger> _logger;
    std::string                     _buffer;
};
#include "rmm/shared/protocol.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace rmm::shared {

    std::string encodeWireMessage(const WireMessage& message)
    {
        std::string out;
        out.reserve(message.type.size() + message.payload.size() + 2);
        out.append(message.type);
        out.push_back(' ');
        out.append(message.payload);
        out.push_back('\n');
        return out;
    }

    std::optional<WireMessage> decodeWireMessage(std::string_view line)
    {
        if (line.empty())
        {
            return std::nullopt;
        }

        const auto spacePos = line.find(' ');
        if (spacePos == std::string_view::npos)
        {
            return std::nullopt;
        }

        WireMessage message;
        message.type = std::string(line.substr(0, spacePos));
        message.payload = std::string(line.substr(spacePos + 1));

        while (!message.payload.empty() &&
               (message.payload.back() == '\r' || message.payload.back() == '\n'))
        {
            message.payload.pop_back();
        }

        if (message.type.empty())
        {
            return std::nullopt;
        }

        return message;
    }

    std::string utcNowIso8601()
    {
        using namespace std::chrono;

        const auto now = system_clock::now();
        const std::time_t t = system_clock::to_time_t(now);

        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

} // namespace rmm::shared
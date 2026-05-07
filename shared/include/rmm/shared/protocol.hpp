#pragma once

#include <optional>
#include <string>

#include "rmm/shared/models.hpp"

namespace rmm::shared
{
    std::string encodeWireMessage(const WireMessage& message);
    std::optional<WireMessage> decodeWireMessage(const std::string& line);
    std::string utcNowIso8601();

    namespace MessageType
    {
        constexpr std::string_view LOGIN = "LOGIN";
        constexpr std::string_view LOGIN_RESP = "LOGIN_RESP";
        constexpr std::string_view REGISTER = "REGISTER";
        constexpr std::string_view REGISTER_RESP = "REGISTER_RESP";
        constexpr std::string_view METRICS = "METRICS";
        constexpr std::string_view ACK = "ACK";
        constexpr std::string_view COMMAND = "COMMAND";
        constexpr std::string_view NODES_LIST = "NODES_LIST";
        constexpr std::string_view NODES_LIST_RESP = "NODES_LIST_RESP";
        constexpr std::string_view GET_METRICS = "GET_METRICS";
        constexpr std::string_view GET_METRICS_RESP = "GET_METRICS_RESP";
    }
} // namespace rmm::shared

#pragma once

#include "models/ClientInfo.hpp"

#include <optional>
#include <string>

namespace rmm::services {
    class AuthService {
    public:
        std::optional<rmm::models::ClientInfo> login(
            const std::string &username,
            const std::string &role
        );
    };
}

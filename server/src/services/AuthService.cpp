#include "services/AuthService.hpp"

namespace rmm::services {
    std::optional<rmm::models::ClientInfo>
    AuthService::login(
        const std::string &username,
        const std::string &role) {
        if (username.empty())
            return std::nullopt;

        if (role != "admin" && role != "user")
            return std::nullopt;

        return rmm::models::ClientInfo{username, role};
    }
}

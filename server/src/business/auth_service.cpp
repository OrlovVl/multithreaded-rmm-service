#include "rmm/server/business/auth_service.hpp"

namespace rmm::server::business {

    bool AuthService::isAdmin(const std::string& username, const std::string& password) const noexcept
    {
        return username == "admin" && password == "admin";
    }

} // namespace rmm::server::business
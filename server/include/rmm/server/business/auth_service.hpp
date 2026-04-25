#pragma once

#include <string>

namespace rmm::server::business {

    class AuthService final
    {
    public:
        bool isAdmin(const std::string& username, const std::string& password) const noexcept;
    };

} // namespace rmm::server::business
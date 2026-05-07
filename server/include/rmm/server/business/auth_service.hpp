#pragma once

#include <optional>
#include "rmm/shared/models.hpp"

namespace rmm::server::data
{
    class MetricRepository;
}

namespace rmm::server::business
{
    class AuthService final
    {
    public:
        explicit AuthService(data::MetricRepository& repository);

        std::optional<rmm::shared::UserInfo> authenticate(const std::string& username,
                                                          const std::string& password) const;

    private:
        data::MetricRepository& m_repository;
    };
} // namespace rmm::server::business

#include "rmm/server/business/auth_service.hpp"
#include "rmm/server/data/metric_repository.hpp"

namespace rmm::server::business {

    AuthService::AuthService(data::MetricRepository& repository)
        : m_repository(repository) {}

    std::optional<rmm::shared::UserInfo> AuthService::authenticate(
        const std::string& username, const std::string& password) const {
        return m_repository.authenticateUser(username, password);
    }

} // namespace rmm::server::business
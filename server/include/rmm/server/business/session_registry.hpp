#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "rmm/shared/models.hpp"

namespace rmm::server::network {
    class Session;
}

namespace rmm::server::business {

    class SessionRegistry final
    {
    public:
        void join(const std::shared_ptr<network::Session>& session);
        void leave(const std::shared_ptr<network::Session>& session);
        void broadcast(const rmm::shared::WireMessage& message);

        std::vector<std::string> nodes() const;

    private:
        mutable std::mutex m_mutex;
        std::vector<std::weak_ptr<network::Session>> m_sessions;
    };

} // namespace rmm::server::business
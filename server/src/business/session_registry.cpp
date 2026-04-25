#include "rmm/server/business/session_registry.hpp"
#include "rmm/server/network/session.hpp"

#include <algorithm>

namespace rmm::server::business {

    void SessionRegistry::join(const std::shared_ptr<network::Session>& session)
    {
        std::scoped_lock lock(m_mutex);
        m_sessions.emplace_back(session);
    }

    void SessionRegistry::leave(const std::shared_ptr<network::Session>& session)
    {
        std::scoped_lock lock(m_mutex);

        m_sessions.erase(
            std::remove_if(m_sessions.begin(), m_sessions.end(),
                [&](const std::weak_ptr<network::Session>& weak) {
                    auto locked = weak.lock();
                    return !locked || locked == session;
                }),
            m_sessions.end());
    }

    void SessionRegistry::broadcast(const rmm::shared::WireMessage& message)
    {
        std::vector<std::shared_ptr<network::Session>> alive;
        {
            std::scoped_lock lock(m_mutex);
            for (auto& weak : m_sessions)
            {
                if (auto locked = weak.lock())
                {
                    alive.push_back(std::move(locked));
                }
            }

            m_sessions.erase(
                std::remove_if(m_sessions.begin(), m_sessions.end(),
                    [](const std::weak_ptr<network::Session>& weak) { return weak.expired(); }),
                m_sessions.end());
        }

        for (auto& session : alive)
        {
            session->deliver(message);
        }
    }

    std::vector<std::string> SessionRegistry::nodes() const
    {
        std::vector<std::string> out;
        std::scoped_lock lock(m_mutex);
        for (const auto& weak : m_sessions)
        {
            if (auto locked = weak.lock())
            {
                out.push_back(locked->nodeName());
            }
        }
        return out;
    }

} // namespace rmm::server::business
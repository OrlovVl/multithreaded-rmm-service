#include "rmm/server/network/server.hpp"

#include "rmm/server/business/auth_service.hpp"
#include "rmm/server/business/session_registry.hpp"
#include "rmm/server/data/metric_repository.hpp"
#include "rmm/server/network/session.hpp"

#include <thread>
#include <vector>

namespace rmm::server::network {

    Server::Server(boost::asio::io_context& ioContext,
                   std::uint16_t port,
                   data::MetricRepository& repository,
                   business::AuthService& authService,
                   business::SessionRegistry& registry)
        : m_acceptor(ioContext, {boost::asio::ip::tcp::v4(), port})
        , m_ioContext(ioContext)
        , m_repository(repository)
        , m_authService(authService)
        , m_registry(registry)
    {
    }

    void Server::run()
    {
        doAccept();

        const auto threadCount = std::max(2u, std::thread::hardware_concurrency());
        std::vector<std::thread> workers;
        workers.reserve(threadCount > 0 ? threadCount - 1 : 1);

        for (unsigned i = 1; i < threadCount; ++i)
        {
            workers.emplace_back([this] { m_ioContext.run(); });
        }

        m_ioContext.run();

        for (auto& worker : workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

    void Server::doAccept()
    {
        m_acceptor.async_accept(
            [this](const boost::system::error_code& ec, boost::asio::ip::tcp::socket socket) {
                if (!ec)
                {
                    auto session = std::make_shared<Session>(
                        std::move(socket),
                        m_repository,
                        m_authService,
                        m_registry);
                    session->start();
                }

                doAccept();
            });
    }

} // namespace rmm::server::network
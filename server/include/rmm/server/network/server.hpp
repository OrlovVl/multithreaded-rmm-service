#pragma once

#include <boost/asio.hpp>

#include <memory>
#include <string>

namespace rmm::server::business {
    class AuthService;
    class SessionRegistry;
}
namespace rmm::server::data {
    class MetricRepository;
}
namespace rmm::server::network {

    class Server final {
    public:
        Server(boost::asio::io_context& ioContext,
               std::uint16_t port,
               data::MetricRepository& repository,
               business::AuthService& authService,
               business::SessionRegistry& registry);

        void run();

    private:
        void doAccept();
        void schedulePurge();

        boost::asio::ip::tcp::acceptor m_acceptor;
        boost::asio::steady_timer m_purgeTimer;
        boost::asio::io_context& m_ioContext;

        data::MetricRepository& m_repository;
        business::AuthService& m_authService;
        business::SessionRegistry& m_registry;
    };

} // namespace rmm::server::network
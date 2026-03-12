#pragma once

#include <boost/asio.hpp>

#include <map>
#include <memory>
#include <mutex>

#include "services/AuthService.hpp"
#include "services/MetricsService.hpp"

namespace rmm::net {
    using tcp = boost::asio::ip::tcp;

    class Session;

    class Server {
        void doAccept();

        boost::asio::io_context &context;

        tcp::acceptor acceptor;

        std::map<std::string, std::weak_ptr<Session> > sessions;

        std::mutex mutex;

        rmm::services::AuthService authService;

        std::unique_ptr<rmm::services::MetricsService> metricsService;

    public:
        Server(
            boost::asio::io_context &context,
            short port,
            std::shared_ptr<rmm::repository::IMetricsRepository> repo
        );

        void startAccept();

        void registerSession(const std::string &username, std::shared_ptr<Session> session);

        void unregisterSession(const std::string &username);

        std::shared_ptr<Session> findSession(const std::string &username);

        rmm::services::MetricsService &getMetricsService();

        rmm::services::AuthService &getAuthService();
    };
}

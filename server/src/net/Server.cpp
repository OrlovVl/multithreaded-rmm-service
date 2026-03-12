#include "net/Server.hpp"
#include "net/Session.hpp"

namespace rmm::net {
    Server::Server(
        boost::asio::io_context &context,
        short port,
        std::shared_ptr<rmm::repository::IMetricsRepository> repo)

        : context(context),
          acceptor(context, tcp::endpoint(tcp::v4(), port)) {
        metricsService = std::make_unique<rmm::services::MetricsService>(repo);
    }

    void Server::startAccept() {
        doAccept();
    }

    void Server::doAccept() {
        acceptor.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    auto session = std::make_shared<Session>(std::move(socket), *this);

                    session->start();
                }

                doAccept();
            }
        );
    }

    void Server::registerSession(
        const std::string &username,
        std::shared_ptr<Session> session) {
        std::lock_guard<std::mutex> lock(mutex);

        sessions[username] = session;
    }

    void Server::unregisterSession(const std::string &username) {
        std::lock_guard<std::mutex> lock(mutex);

        sessions.erase(username);
    }

    std::shared_ptr<Session> Server::findSession(const std::string &username) {
        std::lock_guard<std::mutex> lock(mutex);

        if (!sessions.count(username))
            return nullptr;

        return sessions[username].lock();
    }

    rmm::services::MetricsService &Server::getMetricsService() {
        return *metricsService;
    }

    rmm::services::AuthService &Server::getAuthService() {
        return authService;
    }
}

#pragma once

#include <boost/asio.hpp>

#include <deque>
#include <memory>
#include <string>

#include "rmm/shared/models.hpp"

namespace rmm::server::business {
    class AuthService;
    class SessionRegistry;
}
namespace rmm::server::data {
    class MetricRepository;
}

namespace rmm::server::network {

    class Session final : public std::enable_shared_from_this<Session>
    {
    public:
        Session(boost::asio::ip::tcp::socket socket,
                data::MetricRepository& repository,
                business::AuthService& authService,
                business::SessionRegistry& registry);

        void start();
        void stop();

        void deliver(const rmm::shared::WireMessage& message);
        std::string nodeName() const;
        bool isAdmin() const { return m_isAdmin; }

    private:
        void doRead();
        void onRead(const boost::system::error_code& ec, std::size_t bytesTransferred);
        void doWrite();
        void sendMessage(const rmm::shared::WireMessage& message);
        void handleMessage(const rmm::shared::WireMessage& message);
        void handleHello(const std::string& payload);
        void handleAuth(const std::string& payload);
        void handleMetrics(const std::string& payload);
        void handleCommand(const std::string& payload);
        void handleNodesList();
        void handleGetMetrics(const std::string& payload);

        boost::asio::ip::tcp::socket m_socket;
        boost::asio::strand<boost::asio::io_context::executor_type> m_strand;
        boost::asio::streambuf m_buffer;
        std::deque<std::string> m_outgoing;

        data::MetricRepository& m_repository;
        business::AuthService& m_authService;
        business::SessionRegistry& m_registry;

        bool m_stopped{false};
        bool m_isAdmin{false};
        std::string m_nodeName{"unknown"};
    };

} // namespace rmm::server::network
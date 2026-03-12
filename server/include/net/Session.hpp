#pragma once

#include <boost/asio.hpp>
#include <deque>
#include <memory>
#include <optional>
#include <string>

#include "models/ClientInfo.hpp"

namespace rmm::net {
    using tcp = boost::asio::ip::tcp;

    class Server;

    class Session : public std::enable_shared_from_this<Session> {
        void doRead();

        void doWrite();

        void handleLine(const std::string &line);

        tcp::socket socket;

        boost::asio::streambuf buffer;

        std::deque<std::string> writeQueue;

        Server &server;

        std::optional<rmm::models::ClientInfo> client;

    public:
        Session(tcp::socket socket, Server &server);

        void start();

        void deliver(const std::string &message);

        std::optional<rmm::models::ClientInfo> getClientInfo() const;

        void setClientInfo(const rmm::models::ClientInfo &info);
    };
}

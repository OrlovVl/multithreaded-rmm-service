#include "net/Session.hpp"
#include "net/Server.hpp"
#include "models/Metric.hpp"

#include <boost/system/error_code.hpp>
#include <istream>
#include <iostream>

namespace rmm::net {
    Session::Session(tcp::socket socket, Server &server)
        : socket(std::move(socket)), server(server) {
    }

    void Session::start() {
        doRead();
    }

    std::optional<rmm::models::ClientInfo> Session::getClientInfo() const {
        return client;
    }

    void Session::setClientInfo(const rmm::models::ClientInfo &info) {
        client = info;
    }

    void Session::doRead() {
        auto self = shared_from_this();

        boost::asio::async_read_until(
            socket,
            buffer,
            "\n",
            [this, self](const boost::system::error_code &ec, std::size_t /*length*/) {
                if (ec) {
                    if (client) {
                        server.unregisterSession(client->username);
                    }
                    return;
                }

                std::istream is(&buffer);
                std::string line;
                std::getline(is, line);

                // Удаляем \r если он есть
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                if (!line.empty()) {
                    handleLine(line);
                }

                doRead();
            }
        );
    }

    void Session::handleLine(const std::string &line) {
        auto pos = line.find('|');
        if (pos == std::string::npos)
            return;

        std::string command = line.substr(0, pos);

        if (command == "LOGIN") {
            auto pos2 = line.find('|', pos + 1);
            if (pos2 == std::string::npos)
                return;

            std::string username = line.substr(pos + 1, pos2 - pos - 1);
            std::string role = line.substr(pos2 + 1);

            if (auto result = server.getAuthService().login(username, role)) {
                client = *result;
                server.registerSession(username, shared_from_this());
                deliver("OK_LOGIN|" + username + "\n");
            } else {
                deliver("ERR_LOGIN\n");
            }
        } else if (command == "METRIC") {
            if (!client)
                return;

            std::string payload = line.substr(pos + 1);
            auto metric = rmm::models::Metric::parse(payload);
            server.getMetricsService().acceptMetric(client->username, metric);
        }
    }

    void Session::deliver(const std::string &message) {
        auto self = shared_from_this();
        boost::asio::post(socket.get_executor(), [this, self, message]() {
            bool writing = !writeQueue.empty();
            writeQueue.push_back(message);
            if (!writing) {
                doWrite();
            }
        });
    }

    void Session::doWrite() {
        auto self = shared_from_this();

        boost::asio::async_write(
            socket,
            boost::asio::buffer(writeQueue.front()),
            [this, self](const boost::system::error_code &ec, std::size_t /*length*/) {
                if (ec) {
                    if (client) server.unregisterSession(client->username);
                    return;
                }

                writeQueue.pop_front();
                if (!writeQueue.empty()) {
                    doWrite();
                }
            }
        );
    }
} // namespace rmm::net

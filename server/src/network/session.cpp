#include "rmm/server/network/session.hpp"

#include "rmm/server/business/auth_service.hpp"
#include "rmm/server/business/session_registry.hpp"
#include "rmm/server/data/metric_repository.hpp"
#include "rmm/shared/protocol.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <iostream>
#include <sstream>
#include <locale>

namespace rmm::server::network {

using boost::property_tree::ptree;

namespace {

// Вспомогательная функция для преобразования double в строку с точкой (локаль "C")
std::string doubleToJsonString(double value) {
    std::ostringstream oss;
    oss.imbue(std::locale("C"));
    oss << value;
    return oss.str();
}

ptree parseJson(const std::string& payload) {
    ptree tree;
    std::istringstream iss(payload);
    boost::property_tree::read_json(iss, tree);
    return tree;
}

std::string makeJson(const ptree& tree) {
    std::ostringstream oss;
    boost::property_tree::write_json(oss, tree, false);
    auto s = oss.str();
    if (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

// Ручное построение JSON ответа для GET_METRICS_RESP, чтобы избежать проблем с локалью
std::string buildMetricsJson(const rmm::shared::MetricsSnapshot& snap) {
    std::ostringstream oss;
    oss.imbue(std::locale("C")); // гарантируем точку в числах

    oss << "{"
        << "\"nodeName\":\"" << snap.nodeName << "\","
        << "\"timestampUtc\":\"" << snap.timestampUtc << "\","
        << "\"cpuUsage\":" << doubleToJsonString(snap.cpuUsage) << ","
        << "\"ramUsage\":" << doubleToJsonString(snap.ramUsage) << ","
        << "\"diskFreePercent\":" << doubleToJsonString(snap.diskFreePercent) << ","
        << "\"diskFreeGb\":" << doubleToJsonString(snap.diskFreeGb) << ","
        << "\"temperatureC\":" << doubleToJsonString(snap.temperatureC) << ","
        << "\"smartPredictFailure\":" << (snap.smartPredictFailure ? "true" : "false") << ","
        << "\"processes\":[";

    bool first = true;
    for (const auto& proc : snap.processes) {
        if (!first) oss << ",";
        first = false;
        oss << "{\"pid\":" << proc.pid << ",\"name\":\"" << proc.name << "\"}";
    }

    oss << "]}";
    return oss.str();
}

} // namespace

Session::Session(boost::asio::ip::tcp::socket socket,
                 data::MetricRepository& repository,
                 business::AuthService& authService,
                 business::SessionRegistry& registry)
    : m_socket(std::move(socket))
    , m_strand(boost::asio::make_strand(m_socket.get_executor()))
    , m_repository(repository)
    , m_authService(authService)
    , m_registry(registry) {
    std::cout << "[SESSION] Created session for " << m_socket.remote_endpoint() << std::endl;
}

void Session::start() {
    m_registry.join(shared_from_this());
    doRead();
}

void Session::stop() {
    if (m_stopped) return;
    m_stopped = true;
    std::cout << "[SESSION] Stopping session for " << m_socket.remote_endpoint()
              << " (node: " << m_nodeName << ")" << std::endl;
    boost::system::error_code ec;
    m_socket.close(ec);
    m_registry.leave(shared_from_this());
}

void Session::doRead() {
    auto self = shared_from_this();
    boost::asio::async_read_until(
        m_socket, m_buffer, '\n',
        boost::asio::bind_executor(m_strand,
            [self](const boost::system::error_code& ec, std::size_t transferred) {
                self->onRead(ec, transferred);
            }));
}

void Session::onRead(const boost::system::error_code& ec, std::size_t) {
    if (ec) {
        std::cerr << "[SESSION] Read error: " << ec.message() << std::endl;
        stop();
        return;
    }
    std::istream is(&m_buffer);
    std::string line;
    std::getline(is, line);
    if (auto msg = rmm::shared::decodeWireMessage(line)) {
        std::cout << "[SESSION] Received " << msg->type << std::endl;
        handleMessage(*msg);
    }
    doRead();
}

void Session::handleMessage(const rmm::shared::WireMessage& message) {
    if (message.type == rmm::shared::MessageType::LOGIN) {
        handleLogin(message.payload);
    } else if (message.type == rmm::shared::MessageType::REGISTER) {
        handleRegister(message.payload);
    } else if (message.type == rmm::shared::MessageType::METRICS) {
        handleMetrics(message.payload);
    } else if (message.type == rmm::shared::MessageType::COMMAND) {
        handleCommand(message.payload);
    } else if (message.type == rmm::shared::MessageType::NODES_LIST) {
        handleNodesList();
    } else if (message.type == rmm::shared::MessageType::GET_METRICS) {
        handleGetMetrics(message.payload);
    }
}

void Session::handleLogin(const std::string& payload) {
    auto tree = parseJson(payload);
    std::string username = tree.get<std::string>("username", "");
    std::string password = tree.get<std::string>("password", "");

    auto user = m_authService.authenticate(username, password);
    if (!user) {
        ptree resp;
        resp.put("status", "fail");
        sendMessage({std::string(rmm::shared::MessageType::LOGIN_RESP), makeJson(resp)});
        return;
    }

    m_userId = user->id;
    m_nodeName = user->nodeName;
    m_isAdmin = (user->role == rmm::shared::Role::Admin);

    // Для LOGIN_RESP тоже используем безопасную генерацию (userId целое, проблем с локалью обычно нет,
    // но на всякий случай сформируем вручную)
    std::ostringstream json;
    json.imbue(std::locale("C"));
    json << "{"
         << "\"status\":\"ok\","
         << "\"role\":\"" << (m_isAdmin ? "admin" : "user") << "\","
         << "\"nodeName\":\"" << m_nodeName << "\","
         << "\"userId\":" << m_userId
         << "}";

    sendMessage({std::string(rmm::shared::MessageType::LOGIN_RESP), json.str()});
    std::cout << "[SESSION] User '" << user->username << "' logged in as "
              << (m_isAdmin ? "admin" : "user") << " on node " << m_nodeName << std::endl;
}

void Session::handleRegister(const std::string& payload) {
    auto tree = parseJson(payload);
    std::string username = tree.get<std::string>("username", "");
    std::string password = tree.get<std::string>("password", "");

    auto user = m_repository.registerUser(username, password);
    if (!user) {
        ptree resp;
        resp.put("status", "fail");
        resp.put("message", "Username already taken");
        sendMessage({std::string(rmm::shared::MessageType::REGISTER_RESP), makeJson(resp)});
        return;
    }

    std::ostringstream json;
    json.imbue(std::locale("C"));
    json << "{"
         << "\"status\":\"ok\","
         << "\"role\":\"user\","
         << "\"nodeName\":\"" << user->nodeName << "\","
         << "\"userId\":" << user->id
         << "}";

    sendMessage({std::string(rmm::shared::MessageType::REGISTER_RESP), json.str()});
    std::cout << "[SESSION] Registered new user '" << username << "' -> node " << user->nodeName << std::endl;

    // Автоматический вход
    m_userId = user->id;
    m_nodeName = user->nodeName;
    m_isAdmin = false;
}

void Session::handleMetrics(const std::string& payload) {
    auto tree = parseJson(payload);
    rmm::shared::MetricsSnapshot snap;
    snap.localId = tree.get<std::int64_t>("localId", 0);
    snap.userId = m_userId;
    snap.nodeName = m_nodeName;
    snap.timestampUtc = tree.get<std::string>("timestampUtc", rmm::shared::utcNowIso8601());
    snap.cpuUsage = tree.get<double>("cpuUsage", 0);
    snap.ramUsage = tree.get<double>("ramUsage", 0);
    snap.diskFreePercent = tree.get<double>("diskFreePercent", 0);
    snap.diskFreeGb = tree.get<double>("diskFreeGb", 0);
    snap.temperatureC = tree.get<double>("temperatureC", 0);
    snap.smartPredictFailure = tree.get<bool>("smartPredictFailure", false);
    if (auto procs = tree.get_child_optional("processes")) {
        for (const auto& [_, item] : *procs) {
            snap.processes.push_back({
                item.get<std::int32_t>("pid", 0),
                item.get<std::string>("name", "")
            });
        }
    }

    try {
        m_repository.insertSnapshot(snap, payload);
        std::cout << "[SESSION] METRICS inserted: node=" << snap.nodeName
                  << " cpu=" << snap.cpuUsage << " ram=" << snap.ramUsage
                  << " disk=" << snap.diskFreePercent << " temp=" << snap.temperatureC
                  << " processes=" << snap.processes.size() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[SESSION] Failed to insert snapshot: " << e.what() << std::endl;
    }

    sendMessage({std::string(rmm::shared::MessageType::ACK),
                 "{\"localId\":" + std::to_string(snap.localId) + "}"});
}

void Session::handleCommand(const std::string& payload) {
    if (!m_isAdmin) return;
    std::cout << "[SESSION] Admin " << m_nodeName << " broadcasts command: " << payload << std::endl;
    m_registry.broadcast({std::string(rmm::shared::MessageType::COMMAND), payload});
}

void Session::handleNodesList() {
    if (!m_isAdmin) return;
    auto nodes = m_registry.nodes();
    ptree tree;
    ptree arr;
    for (const auto& name : nodes) {
        ptree item;
        item.put("", name);
        arr.push_back(std::make_pair("", item));
    }
    tree.add_child("nodes", arr);
    sendMessage({std::string(rmm::shared::MessageType::NODES_LIST_RESP), makeJson(tree)});
}

void Session::handleGetMetrics(const std::string& payload) {
    if (!m_isAdmin) return;
    auto tree = parseJson(payload);
    std::string target = tree.get<std::string>("nodeName", "");

    std::cout << "[SESSION] Admin requests metrics for node: " << target << std::endl;
    auto snap = m_repository.getLatestSnapshotForNode(target);
    if (!snap) {
        ptree resp;
        resp.put("error", "no data");
        sendMessage({std::string(rmm::shared::MessageType::GET_METRICS_RESP), makeJson(resp)});
        std::cerr << "[SESSION] No metrics found for node " << target << std::endl;
        return;
    }

    std::cout << "[SESSION] Metrics found: cpu=" << snap->cpuUsage << " ram=" << snap->ramUsage
              << " disk=" << snap->diskFreePercent << " temp=" << snap->temperatureC << std::endl;

    std::string json = buildMetricsJson(*snap);
    sendMessage({std::string(rmm::shared::MessageType::GET_METRICS_RESP), json});
}

void Session::deliver(const rmm::shared::WireMessage& message) {
    sendMessage(message);
}

void Session::sendMessage(const rmm::shared::WireMessage& message) {
    boost::asio::post(m_strand, [self = shared_from_this(), msg = message]() {
        auto data = rmm::shared::encodeWireMessage(msg);
        bool wasEmpty = self->m_outgoing.empty();
        self->m_outgoing.push_back(data);
        if (wasEmpty) self->doWrite();
    });
}

void Session::doWrite() {
    if (m_outgoing.empty() || m_stopped) return;
    auto self = shared_from_this();
    boost::asio::async_write(
        m_socket, boost::asio::buffer(m_outgoing.front()),
        boost::asio::bind_executor(m_strand,
            [self](const boost::system::error_code& ec, std::size_t) {
                if (ec) {
                    self->stop();
                    return;
                }
                self->m_outgoing.pop_front();
                if (!self->m_outgoing.empty()) self->doWrite();
            }));
}

std::string Session::nodeName() const {
    return m_nodeName;
}

} // namespace rmm::server::network
#include "rmm/server/network/session.hpp"

#include "rmm/server/business/auth_service.hpp"
#include "rmm/server/business/session_registry.hpp"
#include "rmm/server/data/metric_repository.hpp"
#include "rmm/shared/protocol.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <sstream>

namespace rmm::server::network {

using boost::property_tree::ptree;

namespace {

ptree parseJson(const std::string& payload)
{
    ptree tree;
    std::istringstream iss(payload);
    boost::property_tree::read_json(iss, tree);
    return tree;
}

std::string makeJson(const ptree& tree)
{
    std::ostringstream oss;
    boost::property_tree::write_json(oss, tree, false);
    auto s = oss.str();
    if (!s.empty() && s.back() == '\n')
        s.pop_back();
    return s;
}

std::string makeJson(const std::string& key, const std::string& value)
{
    ptree tree;
    tree.put(key, value);
    return makeJson(tree);
}

} // namespace

Session::Session(boost::asio::ip::tcp::socket socket,
                 data::MetricRepository& repository,
                 business::AuthService& authService,
                 business::SessionRegistry& registry)
    : m_socket(std::move(socket))
    , m_strand(m_socket.get_executor())
    , m_repository(repository)
    , m_authService(authService)
    , m_registry(registry)
{
}

void Session::start()
{
    m_registry.join(shared_from_this());
    sendMessage({std::string(rmm::shared::MessageType::WELCOME), R"({"role":"user"})"});
    doRead();
}

void Session::stop()
{
    if (m_stopped)
        return;

    m_stopped = true;
    boost::system::error_code ec;
    m_socket.close(ec);
    m_registry.leave(shared_from_this());
}

void Session::doRead()
{
    auto self = shared_from_this();
    boost::asio::async_read_until(
        m_socket,
        m_buffer,
        '\n',
        boost::asio::bind_executor(m_strand,
            [self](const boost::system::error_code& ec, std::size_t bytesTransferred) {
                self->onRead(ec, bytesTransferred);
            }));
}

void Session::onRead(const boost::system::error_code& ec, std::size_t)
{
    if (ec)
    {
        stop();
        return;
    }

    std::istream is(&m_buffer);
    std::string line;
    std::getline(is, line);

    if (auto message = rmm::shared::decodeWireMessage(line))
    {
        handleMessage(*message);
    }

    doRead();
}

void Session::handleMessage(const rmm::shared::WireMessage& message)
{
    using namespace rmm::shared::MessageType;

    if (message.type == HELLO)
        handleHello(message.payload);
    else if (message.type == AUTH)
        handleAuth(message.payload);
    else if (message.type == METRICS)
        handleMetrics(message.payload);
    else if (message.type == COMMAND)
        handleCommand(message.payload);
    else if (message.type == NODES_LIST)
        handleNodesList();
    else if (message.type == GET_METRICS)
        handleGetMetrics(message.payload);
}

void Session::handleHello(const std::string& payload)
{
    const auto tree = parseJson(payload);
    m_nodeName = tree.get<std::string>("nodeName", "unknown");
}

void Session::handleAuth(const std::string& payload)
{
    const auto tree = parseJson(payload);
    const auto username = tree.get<std::string>("username", "");
    const auto password = tree.get<std::string>("password", "");

    const bool ok = m_authService.isAdmin(username, password);
    m_isAdmin = ok;

    const std::string response = ok ? R"({"role":"admin","status":"ok"})"
                                    : R"({"role":"user","status":"fail"})";
    sendMessage({std::string(rmm::shared::MessageType::AUTH_RESULT), response});
}

void Session::handleMetrics(const std::string& payload)
{
    const auto tree = parseJson(payload);

    rmm::shared::MetricsSnapshot snapshot;
    snapshot.localId = tree.get<std::int64_t>("localId", 0);
    snapshot.nodeName = tree.get<std::string>("nodeName", m_nodeName);
    snapshot.timestampUtc = tree.get<std::string>("timestampUtc", rmm::shared::utcNowIso8601());
    snapshot.cpuUsage = tree.get<double>("cpuUsage", 0.0);
    snapshot.ramUsage = tree.get<double>("ramUsage", 0.0);
    snapshot.diskFreePercent = tree.get<double>("diskFreePercent", 0.0);
    snapshot.diskFreeGb = tree.get<double>("diskFreeGb", 0.0);
    snapshot.temperatureC = tree.get<double>("temperatureC", 0.0);
    snapshot.smartPredictFailure = tree.get<bool>("smartPredictFailure", false);

    if (auto child = tree.get_child_optional("processes"))
    {
        for (const auto& [_, node] : *child)
        {
            rmm::shared::ProcessInfo process;
            process.pid = node.get<std::int32_t>("pid", 0);
            process.name = node.get<std::string>("name", "");
            snapshot.processes.push_back(std::move(process));
        }
    }

    m_repository.insertSnapshot(snapshot, payload);
    sendMessage({std::string(rmm::shared::MessageType::ACK),
                 std::string("{\"localId\":") + std::to_string(snapshot.localId) + "}"});
}

void Session::handleCommand(const std::string& payload)
{
    if (!m_isAdmin)
        return;

    // Пересылаем команду всем сессиям (или конкретному узлу)
    m_registry.broadcast({std::string(rmm::shared::MessageType::COMMAND), payload});
}

void Session::handleNodesList()
{
    if (!m_isAdmin)
        return;

    auto nodes = m_registry.nodes();
    ptree tree;
    ptree array;
    for (const auto& name : nodes)
    {
        ptree item;
        item.put("", name);
        array.push_back(std::make_pair("", item));
    }
    tree.add_child("nodes", array);
    sendMessage({std::string(rmm::shared::MessageType::NODES_LIST_RESP), makeJson(tree)});
}

void Session::handleGetMetrics(const std::string& payload)
{
    if (!m_isAdmin)
        return;

    const auto tree = parseJson(payload);
    std::string targetNode = tree.get<std::string>("nodeName", "");

    auto snapshot = m_repository.getLatestSnapshotForNode(targetNode);
    if (!snapshot)
    {
        // Отправляем пустой объект с ошибкой
        ptree resp;
        resp.put("error", "no data");
        sendMessage({std::string(rmm::shared::MessageType::GET_METRICS_RESP), makeJson(resp)});
        return;
    }

    // Сериализуем в JSON
    ptree resp;
    resp.put("nodeName", snapshot->nodeName);
    resp.put("timestampUtc", snapshot->timestampUtc);
    resp.put("cpuUsage", snapshot->cpuUsage);
    resp.put("ramUsage", snapshot->ramUsage);
    resp.put("diskFreePercent", snapshot->diskFreePercent);
    resp.put("diskFreeGb", snapshot->diskFreeGb);
    resp.put("temperatureC", snapshot->temperatureC);
    resp.put("smartPredictFailure", snapshot->smartPredictFailure);

    ptree processesArray;
    for (const auto& proc : snapshot->processes)
    {
        ptree p;
        p.put("pid", proc.pid);
        p.put("name", proc.name);
        processesArray.push_back(std::make_pair("", p));
    }
    resp.add_child("processes", processesArray);

    sendMessage({std::string(rmm::shared::MessageType::GET_METRICS_RESP), makeJson(resp)});
}

void Session::deliver(const rmm::shared::WireMessage& message)
{
    sendMessage(message);
}

void Session::sendMessage(const rmm::shared::WireMessage& message)
{
    boost::asio::post(m_strand,
        [self = shared_from_this(), msg = message]() {
            const auto data = rmm::shared::encodeWireMessage(msg);
            const bool writing = !self->m_outgoing.empty();
            self->m_outgoing.push_back(data);
            if (!writing)
                self->doWrite();
        });
}

void Session::doWrite()
{
    if (m_outgoing.empty() || m_stopped)
        return;

    auto self = shared_from_this();
    boost::asio::async_write(
        m_socket,
        boost::asio::buffer(m_outgoing.front()),
        boost::asio::bind_executor(m_strand,
            [self](const boost::system::error_code& ec, std::size_t) {
                if (ec)
                {
                    self->stop();
                    return;
                }
                self->m_outgoing.pop_front();
                if (!self->m_outgoing.empty())
                    self->doWrite();
            }));
}

std::string Session::nodeName() const
{
    return m_nodeName;
}

} // namespace rmm::server::network
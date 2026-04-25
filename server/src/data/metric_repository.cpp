#include "rmm/server/data/metric_repository.hpp"

#include <sqlite3.h>

#include <stdexcept>

namespace rmm::server::data {

namespace {
void throwOnError(int rc, sqlite3* db, const char* where)
{
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW)
    {
        throw std::runtime_error(std::string(where) + ": " + sqlite3_errmsg(db));
    }
}
} // namespace

MetricRepository::MetricRepository(std::string databasePath)
    : m_databasePath(std::move(databasePath))
{
    open();
    initSchema();
}

MetricRepository::~MetricRepository()
{
    close();
}

void MetricRepository::open()
{
    const int rc = sqlite3_open(m_databasePath.c_str(), &m_db);
    if (rc != SQLITE_OK)
    {
        const std::string error = m_db ? sqlite3_errmsg(m_db) : "cannot open database";
        if (m_db != nullptr)
        {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        throw std::runtime_error(error);
    }
}

void MetricRepository::close()
{
    std::scoped_lock lock(m_mutex);
    if (m_db != nullptr)
    {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

void MetricRepository::initSchema()
{
    std::scoped_lock lock(m_mutex);

    constexpr const char* sql =
        "PRAGMA journal_mode=WAL;"
        "CREATE TABLE IF NOT EXISTS metrics ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  node_name TEXT NOT NULL,"
        "  client_id INTEGER NOT NULL,"
        "  timestamp_utc TEXT NOT NULL,"
        "  cpu_usage REAL NOT NULL,"
        "  ram_usage REAL NOT NULL,"
        "  disk_free_percent REAL NOT NULL,"
        "  disk_free_gb REAL NOT NULL,"
        "  temperature_c REAL NOT NULL,"
        "  smart_predict_failure INTEGER NOT NULL,"
        "  processes_json TEXT NOT NULL,"
        "  raw_payload_json TEXT NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_metrics_node_time ON metrics(node_name, timestamp_utc);";

    char* errMsg = nullptr;
    const int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK)
    {
        std::string message = errMsg ? errMsg : "schema init failed";
        sqlite3_free(errMsg);
        throw std::runtime_error(message);
    }
}

void MetricRepository::insertSnapshot(const rmm::shared::MetricsSnapshot& snapshot, std::string_view rawJson)
{
    std::scoped_lock lock(m_mutex);

    constexpr const char* sql =
        "INSERT INTO metrics (node_name, client_id, timestamp_utc, cpu_usage, ram_usage, "
        "disk_free_percent, disk_free_gb, temperature_c, smart_predict_failure, processes_json, raw_payload_json) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    throwOnError(sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr), m_db, "prepare");

    auto finalize = [&]() {
        if (stmt != nullptr)
        {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    const std::string processesJson = [&]() {
        std::string out = "[";
        for (std::size_t i = 0; i < snapshot.processes.size(); ++i)
        {
            if (i > 0) out.push_back(',');
            out += "{\"pid\":" + std::to_string(snapshot.processes[i].pid) + ",\"name\":\"";
            for (char c : snapshot.processes[i].name)
            {
                if (c == '"') out += "\\\"";
                else if (c == '\\') out += "\\\\";
                else out.push_back(c);
            }
            out += "\"}";
        }
        out.push_back(']');
        return out;
    }();

    int index = 1;
    throwOnError(sqlite3_bind_text(stmt, index++, snapshot.nodeName.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind node");
    throwOnError(sqlite3_bind_int64(stmt, index++, snapshot.localId), m_db, "bind client_id");
    throwOnError(sqlite3_bind_text(stmt, index++, snapshot.timestampUtc.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind ts");
    throwOnError(sqlite3_bind_double(stmt, index++, snapshot.cpuUsage), m_db, "bind cpu");
    throwOnError(sqlite3_bind_double(stmt, index++, snapshot.ramUsage), m_db, "bind ram");
    throwOnError(sqlite3_bind_double(stmt, index++, snapshot.diskFreePercent), m_db, "bind disk%");
    throwOnError(sqlite3_bind_double(stmt, index++, snapshot.diskFreeGb), m_db, "bind disk gb");
    throwOnError(sqlite3_bind_double(stmt, index++, snapshot.temperatureC), m_db, "bind temp");
    throwOnError(sqlite3_bind_int(stmt, index++, snapshot.smartPredictFailure ? 1 : 0), m_db, "bind smart");
    throwOnError(sqlite3_bind_text(stmt, index++, processesJson.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind processes");
    throwOnError(sqlite3_bind_text(stmt, index++, rawJson.data(), static_cast<int>(rawJson.size()), SQLITE_TRANSIENT), m_db, "bind raw");

    throwOnError(sqlite3_step(stmt), m_db, "step");
    finalize();
}

std::vector<std::string> MetricRepository::listNodeNames() const
{
    std::scoped_lock lock(m_mutex);
    std::vector<std::string> nodes;

    constexpr const char* sql = "SELECT DISTINCT node_name FROM metrics ORDER BY node_name ASC;";
    sqlite3_stmt* stmt = nullptr;
    throwOnError(sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr), m_db, "prepare");

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        if (text != nullptr)
        {
            nodes.emplace_back(reinterpret_cast<const char*>(text));
        }
    }

    sqlite3_finalize(stmt);
    return nodes;
}

std::optional<std::string> MetricRepository::latestRawPayloadForNode(const std::string& nodeName) const
{
    std::scoped_lock lock(m_mutex);

    constexpr const char* sql =
        "SELECT raw_payload_json FROM metrics WHERE node_name = ? ORDER BY timestamp_utc DESC, id DESC LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    throwOnError(sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr), m_db, "prepare");
    throwOnError(sqlite3_bind_text(stmt, 1, nodeName.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind");

    std::optional<std::string> result;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        if (text != nullptr)
        {
            result = std::string(reinterpret_cast<const char*>(text));
        }
    }

    sqlite3_finalize(stmt);
    return result;
}

std::optional<rmm::shared::MetricsSnapshot> MetricRepository::getLatestSnapshotForNode(const std::string& nodeName) const
{
    std::scoped_lock lock(m_mutex);

    constexpr const char* sql =
        "SELECT client_id, timestamp_utc, cpu_usage, ram_usage, "
        "disk_free_percent, disk_free_gb, temperature_c, smart_predict_failure, "
        "processes_json FROM metrics WHERE node_name = ? "
        "ORDER BY timestamp_utc DESC, id DESC LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    throwOnError(sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr), m_db, "prepare");
    throwOnError(sqlite3_bind_text(stmt, 1, nodeName.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind");

    std::optional<rmm::shared::MetricsSnapshot> result;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        rmm::shared::MetricsSnapshot snapshot;
        snapshot.nodeName = nodeName;
        snapshot.localId = sqlite3_column_int64(stmt, 0);
        snapshot.timestampUtc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        snapshot.cpuUsage = sqlite3_column_double(stmt, 2);
        snapshot.ramUsage = sqlite3_column_double(stmt, 3);
        snapshot.diskFreePercent = sqlite3_column_double(stmt, 4);
        snapshot.diskFreeGb = sqlite3_column_double(stmt, 5);
        snapshot.temperatureC = sqlite3_column_double(stmt, 6);
        snapshot.smartPredictFailure = sqlite3_column_int(stmt, 7) != 0;

        // Парсим JSON с процессами
        const char* processesJson = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        if (processesJson)
        {
            // Простой ручной парсер для избежания зависимости от boost::property_tree
            std::string json(processesJson);
            // Упрощённо: предполагаем, что это массив объектов {pid, name}
            size_t pos = 0;
            while ((pos = json.find("\"pid\":", pos)) != std::string::npos)
            {
                size_t pidStart = pos + 6;
                while (pidStart < json.size() && std::isspace(json[pidStart])) ++pidStart;
                size_t pidEnd = pidStart;
                while (pidEnd < json.size() && std::isdigit(json[pidEnd])) ++pidEnd;
                if (pidEnd > pidStart)
                {
                    int pid = std::stoi(json.substr(pidStart, pidEnd - pidStart));
                    size_t nameStart = json.find("\"name\":\"", pidEnd);
                    if (nameStart != std::string::npos)
                    {
                        nameStart += 8;
                        size_t nameEnd = nameStart;
                        while (nameEnd < json.size() && json[nameEnd] != '\"') ++nameEnd;
                        std::string name = json.substr(nameStart, nameEnd - nameStart);
                        snapshot.processes.push_back({pid, name});
                    }
                }
                pos = pidEnd;
            }
        }
        result = std::move(snapshot);
    }

    sqlite3_finalize(stmt);
    return result;
}

} // namespace rmm::server::data
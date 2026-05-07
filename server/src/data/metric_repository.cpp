#include "rmm/server/data/metric_repository.hpp"

#include <sqlite3.h>

#include <chrono>
#include <stdexcept>

namespace rmm::server::data {

namespace {
void throwOnError(int rc, sqlite3* db, const char* where) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw std::runtime_error(std::string(where) + ": " + sqlite3_errmsg(db));
    }
}
} // namespace

MetricRepository::MetricRepository(std::string databasePath)
    : m_databasePath(std::move(databasePath)) {
    open();
    initSchema();
    initUsers();
}

MetricRepository::~MetricRepository() {
    close();
}

void MetricRepository::open() {
    const int rc = sqlite3_open(m_databasePath.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        const std::string error = m_db ? sqlite3_errmsg(m_db) : "cannot open database";
        if (m_db != nullptr) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        throw std::runtime_error(error);
    }
}

void MetricRepository::close() {
    std::scoped_lock lock(m_mutex);
    if (m_db != nullptr) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

void MetricRepository::initSchema() {
    std::scoped_lock lock(m_mutex);

    constexpr const char* sql = R"(
        PRAGMA journal_mode=WAL;

        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password TEXT NOT NULL,
            role INTEGER NOT NULL DEFAULT 0,
            node_name TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS metrics (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL,
            node_name TEXT NOT NULL,
            client_id INTEGER NOT NULL,
            timestamp_utc TEXT NOT NULL,
            cpu_usage REAL NOT NULL,
            ram_usage REAL NOT NULL,
            disk_free_percent REAL NOT NULL,
            disk_free_gb REAL NOT NULL,
            temperature_c REAL NOT NULL,
            smart_predict_failure INTEGER NOT NULL,
            processes_json TEXT NOT NULL,
            raw_payload_json TEXT NOT NULL,
            FOREIGN KEY(user_id) REFERENCES users(id)
        );

        CREATE INDEX IF NOT EXISTS idx_metrics_node_time ON metrics(node_name, timestamp_utc);
    )";

    char* errMsg = nullptr;
    const int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string message = errMsg ? errMsg : "schema init failed";
        sqlite3_free(errMsg);
        throw std::runtime_error(message);
    }
}

void MetricRepository::addUser(const std::string& username, const std::string& password,
                               rmm::shared::Role role, const std::string& nodeName) {
    constexpr const char* sql =
        "INSERT OR IGNORE INTO users (username, password, role, node_name) VALUES (?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    throwOnError(sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr), m_db, "prepare addUser");
    throwOnError(sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind username");
    throwOnError(sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind password");
    throwOnError(sqlite3_bind_int(stmt, 3, static_cast<int>(role)), m_db, "bind role");
    throwOnError(sqlite3_bind_text(stmt, 4, nodeName.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind nodeName");
    throwOnError(sqlite3_step(stmt), m_db, "step");
    sqlite3_finalize(stmt);
}

bool MetricRepository::userExists(const std::string& username) const {
    constexpr const char* sql = "SELECT COUNT(*) FROM users WHERE username = ?";
    sqlite3_stmt* stmt = nullptr;
    throwOnError(sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr), m_db, "prepare exists");
    throwOnError(sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind");
    bool exists = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = sqlite3_column_int(stmt, 0) > 0;
    }
    sqlite3_finalize(stmt);
    return exists;
}

void MetricRepository::initUsers() {
    std::scoped_lock lock(m_mutex);
    addUser("admin", "admin", rmm::shared::Role::Admin, "admin-node");
}

std::optional<rmm::shared::UserInfo> MetricRepository::authenticateUser(
    const std::string& username, const std::string& password) const {
    std::scoped_lock lock(m_mutex);

    constexpr const char* sql =
        "SELECT id, username, role, node_name FROM users WHERE username = ? AND password = ?";
    sqlite3_stmt* stmt = nullptr;
    throwOnError(sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr), m_db, "prepare auth");
    throwOnError(sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind user");
    throwOnError(sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind pass");

    std::optional<rmm::shared::UserInfo> user;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        rmm::shared::UserInfo info;
        info.id = sqlite3_column_int64(stmt, 0);
        info.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        info.role = static_cast<rmm::shared::Role>(sqlite3_column_int(stmt, 2));
        info.nodeName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        user = std::move(info);
    }
    sqlite3_finalize(stmt);
    return user;
}

std::optional<rmm::shared::UserInfo> MetricRepository::registerUser(
    const std::string& username, const std::string& password) {
    std::scoped_lock lock(m_mutex);

    if (userExists(username)) {
        return std::nullopt;
    }

    // Генерируем имя узла: username + "-node"
    std::string nodeName = username + "-node";

    constexpr const char* sql =
        "INSERT INTO users (username, password, role, node_name) VALUES (?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    throwOnError(sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr), m_db, "prepare register");
    throwOnError(sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind username");
    throwOnError(sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind password");
    throwOnError(sqlite3_bind_int(stmt, 3, static_cast<int>(rmm::shared::Role::User)), m_db, "bind role");
    throwOnError(sqlite3_bind_text(stmt, 4, nodeName.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind nodeName");
    throwOnError(sqlite3_step(stmt), m_db, "step");
    sqlite3_finalize(stmt);

    rmm::shared::UserInfo info;
    info.id = sqlite3_last_insert_rowid(m_db);
    info.username = username;
    info.role = rmm::shared::Role::User;
    info.nodeName = nodeName;
    return info;
}

std::string MetricRepository::getUserNodeName(std::int64_t userId) const {
    std::scoped_lock lock(m_mutex);
    constexpr const char* sql = "SELECT node_name FROM users WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    throwOnError(sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr), m_db, "prepare getNodeName");
    throwOnError(sqlite3_bind_int64(stmt, 1, userId), m_db, "bind userId");
    std::string nodeName;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        nodeName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return nodeName;
}

void MetricRepository::insertSnapshot(const rmm::shared::MetricsSnapshot& snapshot,
                                      std::string_view rawJson) {
    std::scoped_lock lock(m_mutex);

    constexpr const char* sql = R"(
        INSERT INTO metrics (user_id, node_name, client_id, timestamp_utc, cpu_usage, ram_usage,
                             disk_free_percent, disk_free_gb, temperature_c, smart_predict_failure,
                             processes_json, raw_payload_json)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt = nullptr;
    throwOnError(sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr), m_db, "prepare insert");

    auto finalize = [&]() {
        if (stmt) { sqlite3_finalize(stmt); stmt = nullptr; }
    };

    const std::string processesJson = [&]() {
        std::string out = "[";
        for (size_t i = 0; i < snapshot.processes.size(); ++i) {
            if (i > 0) out += ',';
            out += "{\"pid\":" + std::to_string(snapshot.processes[i].pid) + ",\"name\":\"";
            for (char c : snapshot.processes[i].name) {
                if (c == '"') out += "\\\"";
                else if (c == '\\') out += "\\\\";
                else out += c;
            }
            out += "\"}";
        }
        out += ']';
        return out;
    }();

    int idx = 1;
    throwOnError(sqlite3_bind_int64(stmt, idx++, snapshot.userId), m_db, "bind userId");
    throwOnError(sqlite3_bind_text(stmt, idx++, snapshot.nodeName.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind node");
    throwOnError(sqlite3_bind_int64(stmt, idx++, snapshot.localId), m_db, "bind clientId");
    throwOnError(sqlite3_bind_text(stmt, idx++, snapshot.timestampUtc.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind ts");
    throwOnError(sqlite3_bind_double(stmt, idx++, snapshot.cpuUsage), m_db, "bind cpu");
    throwOnError(sqlite3_bind_double(stmt, idx++, snapshot.ramUsage), m_db, "bind ram");
    throwOnError(sqlite3_bind_double(stmt, idx++, snapshot.diskFreePercent), m_db, "bind disk%");
    throwOnError(sqlite3_bind_double(stmt, idx++, snapshot.diskFreeGb), m_db, "bind diskGb");
    throwOnError(sqlite3_bind_double(stmt, idx++, snapshot.temperatureC), m_db, "bind temp");
    throwOnError(sqlite3_bind_int(stmt, idx++, snapshot.smartPredictFailure ? 1 : 0), m_db, "bind smart");
    throwOnError(sqlite3_bind_text(stmt, idx++, processesJson.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind proc");
    throwOnError(sqlite3_bind_text(stmt, idx++, rawJson.data(), static_cast<int>(rawJson.size()), SQLITE_TRANSIENT), m_db, "bind raw");

    throwOnError(sqlite3_step(stmt), m_db, "step");
    finalize();
}

std::vector<std::string> MetricRepository::listNodeNames() const {
    std::scoped_lock lock(m_mutex);
    std::vector<std::string> nodes;
    constexpr const char* sql = "SELECT DISTINCT node_name FROM metrics ORDER BY node_name ASC";
    sqlite3_stmt* stmt = nullptr;
    throwOnError(sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr), m_db, "prepare list");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nodes.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
    return nodes;
}

std::optional<std::string> MetricRepository::latestRawPayloadForNode(const std::string& nodeName) const {
    std::scoped_lock lock(m_mutex);
    constexpr const char* sql =
        "SELECT raw_payload_json FROM metrics WHERE node_name = ? ORDER BY timestamp_utc DESC, id DESC LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    throwOnError(sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr), m_db, "prepare raw");
    throwOnError(sqlite3_bind_text(stmt, 1, nodeName.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind node");
    std::optional<std::string> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<rmm::shared::MetricsSnapshot> MetricRepository::getLatestSnapshotForNode(
    const std::string& nodeName) const {
    std::scoped_lock lock(m_mutex);
    constexpr const char* sql =
        "SELECT user_id, client_id, timestamp_utc, cpu_usage, ram_usage, disk_free_percent, disk_free_gb, "
        "temperature_c, smart_predict_failure, processes_json FROM metrics "
        "WHERE node_name = ? ORDER BY timestamp_utc DESC, id DESC LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    throwOnError(sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr), m_db, "prepare get");
    throwOnError(sqlite3_bind_text(stmt, 1, nodeName.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind node");
    std::optional<rmm::shared::MetricsSnapshot> snap;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        rmm::shared::MetricsSnapshot s;
        s.userId = sqlite3_column_int64(stmt, 0);
        s.nodeName = nodeName;
        s.localId = sqlite3_column_int64(stmt, 1);
        s.timestampUtc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        s.cpuUsage = sqlite3_column_double(stmt, 3);
        s.ramUsage = sqlite3_column_double(stmt, 4);
        s.diskFreePercent = sqlite3_column_double(stmt, 5);
        s.diskFreeGb = sqlite3_column_double(stmt, 6);
        s.temperatureC = sqlite3_column_double(stmt, 7);
        s.smartPredictFailure = sqlite3_column_int(stmt, 8) != 0;

        const char* procJson = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        if (procJson) {
            std::string json(procJson);
            size_t pos = 0;
            while ((pos = json.find("\"pid\":", pos)) != std::string::npos) {
                size_t ps = pos + 6;
                while (ps < json.size() && std::isspace(json[ps])) ++ps;
                size_t pe = ps;
                while (pe < json.size() && std::isdigit(json[pe])) ++pe;
                if (pe > ps) {
                    int pid = std::stoi(json.substr(ps, pe - ps));
                    size_t ns = json.find("\"name\":\"", pe);
                    if (ns != std::string::npos) {
                        ns += 8;
                        size_t ne = ns;
                        while (ne < json.size() && json[ne] != '\"') ++ne;
                        std::string name = json.substr(ns, ne - ns);
                        s.processes.push_back({pid, name});
                    }
                }
                pos = pe;
            }
        }
        snap = std::move(s);
    }
    sqlite3_finalize(stmt);
    return snap;
}

void MetricRepository::purgeOldMetrics() {
    std::scoped_lock lock(m_mutex);
    // Удаляем метрики старше 7 дней от текущего момента
    constexpr const char* sql =
        "DELETE FROM metrics WHERE datetime(timestamp_utc) < datetime('now', '-7 days')";
    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string msg = errMsg ? errMsg : "purge failed";
        sqlite3_free(errMsg);
        throw std::runtime_error(msg);
    }
}

} // namespace rmm::server::data
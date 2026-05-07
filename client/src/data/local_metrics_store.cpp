#include "rmm/client/data/local_metrics_store.hpp"

#include <sqlite3.h>

#include <stdexcept>

namespace rmm::client::data {

namespace {
void throwOnError(int rc, sqlite3* db, const char* where)
{
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW)
    {
        throw std::runtime_error(std::string(where) + ": " + sqlite3_errmsg(db));
    }
}
} // namespace

LocalMetricsStore::LocalMetricsStore(std::string databasePath)
    : m_databasePath(std::move(databasePath))
{
    open();
    initSchema();
}

LocalMetricsStore::~LocalMetricsStore()
{
    close();
}

void LocalMetricsStore::open()
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

void LocalMetricsStore::close()
{
    std::scoped_lock lock(m_mutex);
    if (m_db != nullptr)
    {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

void LocalMetricsStore::initSchema()
{
    std::scoped_lock lock(m_mutex);

    constexpr const char* sql =
        "PRAGMA journal_mode=WAL;"
        "CREATE TABLE IF NOT EXISTS outbox ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  created_utc TEXT NOT NULL,"
        "  payload_json TEXT NOT NULL"
        ");";

    char* errMsg = nullptr;
    const int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK)
    {
        std::string message = errMsg ? errMsg : "schema init failed";
        sqlite3_free(errMsg);
        throw std::runtime_error(message);
    }
}

std::int64_t LocalMetricsStore::enqueue(std::string_view payloadJson)
{
    std::scoped_lock lock(m_mutex);

    constexpr const char* sql = "INSERT INTO outbox(created_utc, payload_json) VALUES(?, ?);";
    sqlite3_stmt* stmt = nullptr;
    throwOnError(sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr), m_db, "prepare");

    const std::string now = "CURRENT_TIMESTAMP";

    throwOnError(sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_TRANSIENT), m_db, "bind time");
    throwOnError(sqlite3_bind_text(stmt, 2, payloadJson.data(), static_cast<int>(payloadJson.size()), SQLITE_TRANSIENT), m_db, "bind payload");
    throwOnError(sqlite3_step(stmt), m_db, "step");

    sqlite3_finalize(stmt);
    return sqlite3_last_insert_rowid(m_db);
}

std::optional<PendingItem> LocalMetricsStore::peekOldest() const
{
    std::scoped_lock lock(m_mutex);

    constexpr const char* sql = "SELECT id, payload_json FROM outbox ORDER BY id ASC LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    throwOnError(sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr), m_db, "prepare");

    std::optional<PendingItem> item;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        item.emplace();
        item->id = sqlite3_column_int64(stmt, 0);
        const unsigned char* text = sqlite3_column_text(stmt, 1);
        if (text != nullptr)
        {
            item->payloadJson = reinterpret_cast<const char*>(text);
        }
    }

    sqlite3_finalize(stmt);
    return item;
}

void LocalMetricsStore::erase(std::int64_t id)
{
    std::scoped_lock lock(m_mutex);

    constexpr const char* sql = "DELETE FROM outbox WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    throwOnError(sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr), m_db, "prepare");
    throwOnError(sqlite3_bind_int64(stmt, 1, id), m_db, "bind");
    throwOnError(sqlite3_step(stmt), m_db, "step");
    sqlite3_finalize(stmt);
}

std::size_t LocalMetricsStore::count() const
{
    std::scoped_lock lock(m_mutex);

    constexpr const char* sql = "SELECT COUNT(*) FROM outbox;";
    sqlite3_stmt* stmt = nullptr;
    throwOnError(sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr), m_db, "prepare");

    std::size_t result = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        result = static_cast<std::size_t>(sqlite3_column_int64(stmt, 0));
    }

    sqlite3_finalize(stmt);
    return result;
}

} // namespace rmm::client::data
#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace rmm::client::data {

    struct PendingItem
    {
        std::int64_t id{};
        std::string payloadJson;
    };

    class LocalMetricsStore final
    {
    public:
        explicit LocalMetricsStore(std::string databasePath);
        ~LocalMetricsStore();

        LocalMetricsStore(const LocalMetricsStore&) = delete;
        LocalMetricsStore& operator=(const LocalMetricsStore&) = delete;

        std::int64_t enqueue(std::string_view payloadJson);
        std::optional<PendingItem> peekOldest() const;
        void erase(std::int64_t id);
        std::size_t count() const;

    private:
        void open();
        void close();
        void initSchema();

        std::string m_databasePath;
        sqlite3* m_db{nullptr};
        mutable std::mutex m_mutex;
    };

} // namespace rmm::client::data
#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rmm/shared/models.hpp"

struct sqlite3;

namespace rmm::server::data {

    class MetricRepository final
    {
    public:
        explicit MetricRepository(std::string databasePath);
        ~MetricRepository();

        MetricRepository(const MetricRepository&) = delete;
        MetricRepository& operator=(const MetricRepository&) = delete;

        void insertSnapshot(const rmm::shared::MetricsSnapshot& snapshot, std::string_view rawJson);
        std::vector<std::string> listNodeNames() const;
        std::optional<std::string> latestRawPayloadForNode(const std::string& nodeName) const;
        std::optional<rmm::shared::MetricsSnapshot> getLatestSnapshotForNode(const std::string& nodeName) const;

    private:
        void open();
        void close();
        void initSchema();

        std::string m_databasePath;
        sqlite3* m_db{nullptr};
        mutable std::mutex m_mutex;
    };

} // namespace rmm::server::data
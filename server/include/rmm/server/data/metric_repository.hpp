#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "rmm/shared/models.hpp"

struct sqlite3;

namespace rmm::server::data {

    class MetricRepository final {
    public:
        explicit MetricRepository(std::string databasePath);
        ~MetricRepository();

        MetricRepository(const MetricRepository&) = delete;
        MetricRepository& operator=(const MetricRepository&) = delete;

        // Пользователи
        void initUsers();   // создаёт admin
        std::optional<rmm::shared::UserInfo> authenticateUser(const std::string& username,
                                                              const std::string& password) const;
        // Регистрация нового пользователя (возвращает UserInfo или nullopt, если имя занято)
        std::optional<rmm::shared::UserInfo> registerUser(const std::string& username,
                                                          const std::string& password);
        std::string getUserNodeName(std::int64_t userId) const;

        // Метрики
        void insertSnapshot(const rmm::shared::MetricsSnapshot& snapshot,
                            std::string_view rawJson);
        std::vector<std::string> listNodeNames() const;
        std::optional<std::string> latestRawPayloadForNode(const std::string& nodeName) const;
        std::optional<rmm::shared::MetricsSnapshot> getLatestSnapshotForNode(const std::string& nodeName) const;

        // Очистка старых метрик (> 7 дней)
        void purgeOldMetrics();

    private:
        void open();
        void close();
        void initSchema();

        void addUser(const std::string& username, const std::string& password,
                     rmm::shared::Role role, const std::string& nodeName);
        bool userExists(const std::string& username) const;

        std::string m_databasePath;
        sqlite3* m_db{nullptr};
        mutable std::mutex m_mutex;
    };

} // namespace rmm::server::data
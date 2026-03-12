#include "repository/InMemoryMetricsRepository.hpp"
#include <algorithm>

namespace rmm::repository {
    void InMemoryMetricsRepository::addMetric(
        const std::string &username,
        const rmm::models::Metric &metric) {
        std::lock_guard<std::mutex> lock(mutex);

        auto &userMetrics = storage[username];
        userMetrics.push_back(metric);

        const size_t MAX_METRICS = 100;

        if (userMetrics.size() > MAX_METRICS) {
            userMetrics.pop_front();
        }
    }

    std::vector<rmm::models::Metric>
    InMemoryMetricsRepository::getMetrics(
        const std::string &username) {
        std::lock_guard<std::mutex> lock(mutex);

        if (storage.find(username) == storage.end())
            return {};

        const auto &deq = storage.at(username);

        return std::vector<rmm::models::Metric>(deq.begin(), deq.end());
    }
}

#pragma once

#include <map>
#include <deque>
#include <mutex>

#include "repository/IMetricsRepository.hpp"

namespace rmm::repository {
    class InMemoryMetricsRepository : public IMetricsRepository {
        std::map<std::string, std::deque<rmm::models::Metric> > storage;
        std::mutex mutex;

    public:
        void addMetric(const std::string &username, const rmm::models::Metric &metric) override;

        std::vector<rmm::models::Metric> getMetrics(const std::string &username) override;
    };
}

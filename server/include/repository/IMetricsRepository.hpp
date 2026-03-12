#pragma once

#include <vector>
#include <string>

#include "models/Metric.hpp"

namespace rmm::repository {
    class IMetricsRepository {
    public:
        virtual ~IMetricsRepository() = default;

        virtual void addMetric(
            const std::string &username,
            const rmm::models::Metric &metric
        ) = 0;

        virtual std::vector<rmm::models::Metric> getMetrics(
            const std::string &username
        ) = 0;
    };
}

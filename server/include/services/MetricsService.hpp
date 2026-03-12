#pragma once

#include "repository/IMetricsRepository.hpp"

#include <memory>
#include <string>

namespace rmm::services {
    class MetricsService {
        std::shared_ptr<rmm::repository::IMetricsRepository> repository;

    public:
        explicit MetricsService(
            std::shared_ptr<rmm::repository::IMetricsRepository> repo
        );

        void acceptMetric(
            const std::string &username,
            const rmm::models::Metric &metric
        );

        std::string getSerializedMetrics(
            const std::string &username
        );
    };
}

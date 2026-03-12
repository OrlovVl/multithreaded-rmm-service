#include "services/MetricsService.hpp"

namespace rmm::services {
    MetricsService::MetricsService(
        std::shared_ptr<rmm::repository::IMetricsRepository> repo) {
        repository = repo;
    }

    void MetricsService::acceptMetric(
        const std::string &username,
        const rmm::models::Metric &metric) {
        repository->addMetric(username, metric);
    }

    std::string MetricsService::getSerializedMetrics(
        const std::string &username) {
        auto list = repository->getMetrics(username);

        std::string result;

        bool first = true;

        for (auto &m: list) {
            if (!first) result += "|";

            result += m.serialize();

            first = false;
        }

        return result;
    }
}

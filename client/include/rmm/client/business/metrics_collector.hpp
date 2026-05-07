#pragma once

#include "rmm/shared/models.hpp"

namespace rmm::client::business {

    class MetricsCollector final
    {
    public:
        MetricsCollector();

        rmm::shared::MetricsSnapshot collect(const std::string& nodeName);

    private:
        double collectCpuUsage();
        double collectRamUsage();
        double collectDiskFreePercent();
        double collectDiskFreeGb();
        double collectTemperature();
        bool   collectSmartPredictFailure();
        std::vector<rmm::shared::ProcessInfo> collectProcesses();

        unsigned long long m_prevIdle = 0;
        unsigned long long m_prevKernel = 0;
        unsigned long long m_prevUser = 0;
        bool m_firstCpuSample = true;
    };

} // namespace rmm::client::business
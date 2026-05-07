#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rmm::shared {

    enum class Role {
        User,
        Admin
    };

    struct ProcessInfo {
        std::int32_t pid{};
        std::string name;
    };

    struct UserInfo {
        std::int64_t id{};
        std::string username;
        std::string nodeName;
        Role role{Role::User};
    };

    struct MetricsSnapshot {
        std::int64_t localId{0};
        std::int64_t userId{0};
        std::string nodeName;
        std::string timestampUtc;

        double cpuUsage{0.0};
        double ramUsage{0.0};

        double diskFreePercent{0.0};
        double diskFreeGb{0.0};

        double temperatureC{0.0};
        bool smartPredictFailure{false};

        std::vector<ProcessInfo> processes;
    };

    struct WireMessage {
        std::string type;
        std::string payload;
    };

} // namespace rmm::shared
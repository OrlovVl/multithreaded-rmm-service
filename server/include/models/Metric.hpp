#pragma once

#include <string>
#include <unordered_map>

namespace rmm::models {
    struct Metric {
        std::unordered_map<std::string, std::string> fields;

        std::string serialize() const;

        static Metric parse(const std::string &s);
    };
}

#include "models/Metric.hpp"

namespace rmm::models {
    std::string Metric::serialize() const {
        std::string result;

        bool first = true;

        for (auto &p: fields) {
            if (!first) result += ";";

            result += p.first + "=" + p.second;

            first = false;
        }

        return result;
    }

    Metric Metric::parse(const std::string &s) {
        Metric m;

        size_t pos = 0;

        while (pos < s.size()) {
            auto sep = s.find('=', pos);

            if (sep == std::string::npos) break;

            std::string key = s.substr(pos, sep - pos);

            auto end = s.find(';', sep + 1);

            std::string value = s.substr(sep + 1,
                                         end == std::string::npos ? std::string::npos : end - (sep + 1));

            m.fields[key] = value;

            if (end == std::string::npos) break;

            pos = end + 1;
        }

        return m;
    }
}

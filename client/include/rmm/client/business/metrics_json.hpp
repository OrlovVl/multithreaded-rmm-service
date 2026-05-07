#pragma once

#include <QString>

#include "rmm/shared/models.hpp"

namespace rmm::client::business {

    QString toJson(const rmm::shared::MetricsSnapshot& snapshot);
    rmm::shared::MetricsSnapshot fromJson(const QString& json);

} // namespace rmm::client::business
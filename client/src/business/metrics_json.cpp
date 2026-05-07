#include "rmm/client/business/metrics_json.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace rmm::client::business {

QString toJson(const rmm::shared::MetricsSnapshot& snapshot)
{
    QJsonObject root;
    root["localId"] = static_cast<qint64>(snapshot.localId);
    root["nodeName"] = QString::fromStdString(snapshot.nodeName);
    root["timestampUtc"] = QString::fromStdString(snapshot.timestampUtc);
    root["cpuUsage"] = snapshot.cpuUsage;
    root["ramUsage"] = snapshot.ramUsage;
    root["diskFreePercent"] = snapshot.diskFreePercent;
    root["diskFreeGb"] = snapshot.diskFreeGb;
    root["temperatureC"] = snapshot.temperatureC;
    root["smartPredictFailure"] = snapshot.smartPredictFailure;

    QJsonArray processes;
    for (const auto& process : snapshot.processes)
    {
        QJsonObject item;
        item["pid"] = process.pid;
        item["name"] = QString::fromStdString(process.name);
        processes.append(item);
    }
    root["processes"] = processes;

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

rmm::shared::MetricsSnapshot fromJson(const QString& json)
{
    const auto doc = QJsonDocument::fromJson(json.toUtf8());
    const auto root = doc.object();

    rmm::shared::MetricsSnapshot snapshot;
    snapshot.localId = static_cast<std::int64_t>(root["localId"].toDouble(0.0));
    snapshot.nodeName = root["nodeName"].toString().toStdString();
    snapshot.timestampUtc = root["timestampUtc"].toString().toStdString();
    snapshot.cpuUsage = root["cpuUsage"].toDouble(0.0);
    snapshot.ramUsage = root["ramUsage"].toDouble(0.0);
    snapshot.diskFreePercent = root["diskFreePercent"].toDouble(0.0);
    snapshot.diskFreeGb = root["diskFreeGb"].toDouble(0.0);
    snapshot.temperatureC = root["temperatureC"].toDouble(0.0);
    snapshot.smartPredictFailure = root["smartPredictFailure"].toBool(false);

    const auto processes = root["processes"].toArray();
    for (const auto& value : processes)
    {
        const auto item = value.toObject();
        rmm::shared::ProcessInfo info;
        info.pid = item["pid"].toInt(0);
        info.name = item["name"].toString().toStdString();
        snapshot.processes.push_back(std::move(info));
    }

    return snapshot;
}

} // namespace rmm::client::business
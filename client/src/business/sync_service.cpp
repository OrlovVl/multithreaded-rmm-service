#include "rmm/client/business/sync_service.hpp"

#include "rmm/client/business/metrics_json.hpp"
#include "rmm/client/network/client_channel.hpp"
#include "rmm/client/data/local_metrics_store.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QCoreApplication>

namespace rmm::client::business {

SyncService::SyncService(network::ClientChannel* channel,
                         data::LocalMetricsStore* store,
                         QObject* parent)
    : QObject(parent)
    , m_channel(channel)
    , m_store(store)
{
    connect(m_channel, &network::ClientChannel::messageReceived,
            this, &SyncService::onMessageReceived);
    connect(m_channel, &network::ClientChannel::ackReceived,
            this, &SyncService::onAckReceived);
    connect(m_channel, &network::ClientChannel::serverCommandReceived,
            this, &SyncService::onServerCommand);
}

void SyncService::start(const QString& nodeName, int intervalMs)
{
    m_nodeName = nodeName;

    // Создаём worker в отдельном потоке
    m_worker = std::make_unique<MetricsCollectorWorker>(nodeName);
    m_worker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, m_worker.get(), &QObject::deleteLater);
    connect(m_worker.get(), &MetricsCollectorWorker::snapshotCollected,
            this, &SyncService::onSnapshotFromWorker);
    connect(m_worker.get(), &MetricsCollectorWorker::error,
            this, [this](const QString& err) { emit logMessage(err); });

    m_workerThread.start();
    QMetaObject::invokeMethod(m_worker.get(), "startCollecting", Q_ARG(int, intervalMs));
}

void SyncService::stop()
{
    if (m_worker)
    {
        m_worker->stopCollecting();
        m_workerThread.quit();
        m_workerThread.wait();
        m_worker.reset();
    }
}

void SyncService::onSnapshotFromWorker(const rmm::shared::MetricsSnapshot& snapshot)
{
    emit snapshotCollected(snapshot);

    const auto json = toJson(snapshot);
    const auto id = m_store->enqueue(json.toStdString());

    rmm::shared::MetricsSnapshot copy = snapshot;
    copy.localId = id;
    const auto payload = toJson(copy);

    if (m_channel->isConnected())
    {
        m_channel->sendWireMessage("METRICS", payload);
        emit logMessage(QString("Sent snapshot %1").arg(id));
    }

    flushOutbox();
}

void SyncService::flushOutbox()
{
    if (!m_channel->isConnected())
        return;

    const auto next = m_store->peekOldest();
    if (!next)
        return;

    const QString payload = QString::fromStdString(next->payloadJson);
    m_channel->sendWireMessage("METRICS", payload);
}

void SyncService::onAckReceived(qint64 localId)
{
    if (localId > 0)
    {
        m_store->erase(localId);
        emit logMessage(QString("ACK received for %1").arg(localId));
    }
    flushOutbox();
}

void SyncService::onMessageReceived(const QString& type, const QString& payload)
{
    if (type == "AUTH_RESULT")
    {
        auto doc = QJsonDocument::fromJson(payload.toUtf8());
        if (doc.object().value("role").toString() == "admin")
        {
            m_isAdmin = true;
            emit logMessage("Admin privileges granted");
        }
    }
    else if (type == "NODES_LIST_RESP")
    {
        auto doc = QJsonDocument::fromJson(payload.toUtf8());
        const auto nodesArray = doc.object().value("nodes").toArray();
        QStringList nodes;
        for (const auto& val : nodesArray)
            nodes << val.toString();
        emit nodesListReceived(nodes);
    }
    else if (type == "GET_METRICS_RESP")
    {
        auto doc = QJsonDocument::fromJson(payload.toUtf8());
        if (doc.object().contains("error"))
        {
            emit logMessage("Failed to get metrics: " + doc.object().value("error").toString());
        }
        else
        {
            auto snapshot = fromJson(payload);
            emit nodeMetricsReceived(snapshot);
        }
    }
}

void SyncService::onServerCommand(const QString& payload)
{
    emit logMessage(QString("Server command received: %1").arg(payload));
    processCommand(payload);
}

void SyncService::processCommand(const QString& payload)
{
    auto doc = QJsonDocument::fromJson(payload.toUtf8());
    if (!doc.isObject())
        return;

    auto obj = doc.object();
    QString action = obj.value("action").toString();
    if (action == "refresh")
    {
        // Принудительный сбор
        QMetaObject::invokeMethod(m_worker.get(), "collectOnce");
        emit logMessage("Refresh command executed");
    }
    else if (action == "restart")
    {
        // Перезапуск приложения (осторожно!)
        emit logMessage("Restart command received - restarting...");
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
        QCoreApplication::quit();
    }
    else if (action == "shutdown")
    {
        emit logMessage("Shutdown command received");
        QCoreApplication::quit();
    }
    else if (action == "execute")
    {
        QString cmd = obj.value("command").toString();
        if (!cmd.isEmpty())
        {
            QProcess::startDetached(cmd);
            emit logMessage(QString("Executed: %1").arg(cmd));
        }
    }
    else
    {
        emit logMessage(QString("Unknown command: %1").arg(action));
    }
}

void SyncService::requestNodesList()
{
    if (!m_isAdmin)
        return;
    m_channel->sendWireMessage("NODES_LIST", "{}");
}

void SyncService::requestNodeMetrics(const QString& nodeName)
{
    if (!m_isAdmin)
        return;
    QJsonObject obj{{"nodeName", nodeName}};
    m_channel->sendWireMessage("GET_METRICS", QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

} // namespace rmm::client::business
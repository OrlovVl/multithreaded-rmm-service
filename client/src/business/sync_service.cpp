#include "rmm/client/business/sync_service.hpp"
#include "rmm/client/business/metrics_json.hpp"
#include "rmm/client/network/client_channel.hpp"
#include "rmm/client/data/local_metrics_store.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QCoreApplication>

namespace rmm::client::business
{
    SyncService::SyncService(network::ClientChannel* channel,
                             data::LocalMetricsStore* store,
                             QObject* parent)
        : QObject(parent), m_channel(channel), m_store(store)
    {
        connect(m_channel, &network::ClientChannel::messageReceived, this, &SyncService::onMessageReceived);
        connect(m_channel, &network::ClientChannel::ackReceived, this, &SyncService::onAckReceived);
        connect(m_channel, &network::ClientChannel::serverCommandReceived, this, &SyncService::onServerCommand);
    }

    void SyncService::start(const QString& nodeName, int intervalMs)
    {
        if (m_worker) {
            emit logMessage("Collection already running, ignoring start.");
            return;
        }

        m_nodeName = nodeName;

        m_worker = std::make_unique<MetricsCollectorWorker>(nodeName);
        m_worker->moveToThread(&m_workerThread);

        connect(&m_workerThread, &QThread::finished, m_worker.get(), &QObject::deleteLater);
        connect(m_worker.get(), &MetricsCollectorWorker::snapshotCollected,
                this, &SyncService::onSnapshotFromWorker);
        connect(m_worker.get(), &MetricsCollectorWorker::error,
                this, [this](const QString& err) { emit logMessage(err); });
        // Подключаем вывод логов от воркера к нашему логу
        connect(m_worker.get(), &MetricsCollectorWorker::logMessage,
                this, &SyncService::logMessage);

        m_workerThread.start();

        QMetaObject::invokeMethod(m_worker.get(), [this, intervalMs]() {
            m_worker->startCollecting(intervalMs);
        }, Qt::QueuedConnection);

        emit logMessage(QString("Collection thread started for node %1").arg(nodeName));
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
        setAdmin(false);
    }

    void SyncService::logout() { setAdmin(false); }

    void SyncService::setAdmin(bool isAdmin)
    {
        if (m_isAdmin != isAdmin)
        {
            m_isAdmin = isAdmin;
            emit adminStatusChanged(isAdmin);
        }
    }

    void SyncService::onSnapshotFromWorker(const rmm::shared::MetricsSnapshot& snapshot)
    {
        emit snapshotCollected(snapshot);

        const auto json = toJson(snapshot).toStdString();
        const auto id = m_store->enqueue(json); // сохраняем "сырой" JSON

        emit logMessage(QString("Snapshot %1 enqueued").arg(id));

        flushOutbox();
    }

    void SyncService::flushOutbox()
    {
        if (!m_channel->isConnected())
            return;

        while (true)
        {
            auto next = m_store->peekOldest();
            if (!next)
                break;

            QJsonDocument doc = QJsonDocument::fromJson(
                QString::fromStdString(next->payloadJson).toUtf8());
            QJsonObject obj = doc.object();
            obj["localId"] = static_cast<qint64>(next->id);
            const QString payload = QString::fromUtf8(
                QJsonDocument(obj).toJson(QJsonDocument::Compact));

            m_channel->sendWireMessage("METRICS", payload);
            emit logMessage(QString("Sent snapshot %1").arg(next->id));

            break;
        }
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
        if (type == "LOGIN_RESP")
        {
            auto doc = QJsonDocument::fromJson(payload.toUtf8());
            QString status = doc.object().value("status").toString();
            QString role = doc.object().value("role").toString();
            QString nodeName = doc.object().value("nodeName").toString();
            qint64 userId = static_cast<qint64>(doc.object().value("userId").toDouble(0));
            emit loginResult(status, role, nodeName, userId);

            if (status == "ok")
            {
                bool isAdmin = (role == "admin");
                setAdmin(isAdmin);
                m_nodeName = nodeName;
                m_userId = userId;
                if (!m_worker) start(nodeName, 5000);
            }
        }
        else if (type == "REGISTER_RESP")
        {
            auto doc = QJsonDocument::fromJson(payload.toUtf8());
            QString status = doc.object().value("status").toString();
            QString message = doc.object().value("message").toString();
            QString nodeName = doc.object().value("nodeName").toString();
            qint64 userId = static_cast<qint64>(doc.object().value("userId").toDouble(0));
            emit registerResult(status, message, nodeName, userId);

            if (status == "ok")
            {
                // После успешной регистрации сервер уже аутентифицировал нас,
                // но на клиенте мы не получали LOGIN_RESP. Эмулируем успешный вход:
                setAdmin(false);
                m_nodeName = nodeName;
                m_userId = userId;
                if (!m_worker) start(nodeName, 5000);
            }
        }
        else if (type == "NODES_LIST_RESP")
        {
            auto doc = QJsonDocument::fromJson(payload.toUtf8());
            QStringList nodes;
            for (const auto& val : doc.object().value("nodes").toArray())
                nodes << val.toString();
            emit nodesListReceived(nodes);
        }
        else if (type == "GET_METRICS_RESP")
        {
            auto doc = QJsonDocument::fromJson(payload.toUtf8());
            if (doc.object().contains("error"))
            emit logMessage("Failed to get metrics: " + doc.object().value("error").toString());
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
        if (!doc.isObject()) return;
        auto obj = doc.object();
        QString action = obj.value("action").toString();
        if (action == "refresh")
        {
            QMetaObject::invokeMethod(m_worker.get(), "collectOnce");
            emit logMessage("Refresh command executed");
        }
        else if (action == "restart")
        {
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
        if (!m_isAdmin) return;
        m_channel->sendWireMessage("NODES_LIST", "{}");
    }

    void SyncService::requestNodeMetrics(const QString& nodeName)
    {
        if (!m_isAdmin) return;
        QJsonObject obj{{"nodeName", nodeName}};
        m_channel->sendWireMessage("GET_METRICS", QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    }

} // namespace rmm::client::business

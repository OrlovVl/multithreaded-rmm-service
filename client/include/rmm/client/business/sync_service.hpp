#pragma once

#include <QObject>
#include <QTimer>
#include <QThread>
#include <memory>

#include "rmm/shared/models.hpp"
#include "rmm/client/business/metrics_collector_worker.hpp"

namespace rmm::client::network {
    class ClientChannel;
}
namespace rmm::client::data {
    class LocalMetricsStore;
}
namespace rmm::client::business {

    class SyncService final : public QObject {
        Q_OBJECT

    public:
        SyncService(network::ClientChannel* channel,
                    data::LocalMetricsStore* store,
                    QObject* parent = nullptr);

        void start(const QString& nodeName, int intervalMs = 5000);
        void stop();
        void logout();

        signals:
            void snapshotCollected(const rmm::shared::MetricsSnapshot& snapshot);
        void logMessage(const QString& text);
        void nodesListReceived(const QStringList& nodes);
        void nodeMetricsReceived(const rmm::shared::MetricsSnapshot& snapshot);
        void adminStatusChanged(bool isAdmin);
        void loginResult(const QString& status, const QString& role,
                         const QString& nodeName, qint64 userId);
        void registerResult(const QString& status, const QString& message,
                            const QString& nodeName, qint64 userId);

    public slots:
        void onAckReceived(qint64 localId);
        void onServerCommand(const QString& payload);
        void onMessageReceived(const QString& type, const QString& payload);
        void requestNodesList();
        void requestNodeMetrics(const QString& nodeName);

    private slots:
        void onSnapshotFromWorker(const rmm::shared::MetricsSnapshot& snapshot);

    private:
        void flushOutbox();
        void processCommand(const QString& payload);
        void setAdmin(bool isAdmin);

        network::ClientChannel* m_channel;
        data::LocalMetricsStore* m_store;
        std::unique_ptr<MetricsCollectorWorker> m_worker;
        QThread m_workerThread;
        QString m_nodeName;
        bool m_isAdmin{false};
        qint64 m_userId{0};
    };


} // namespace rmm::client::business
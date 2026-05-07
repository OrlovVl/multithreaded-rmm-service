#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <atomic>

#include "rmm/shared/models.hpp"

namespace rmm::client::business {

    class MetricsCollector;

    class MetricsCollectorWorker final : public QObject
    {
        Q_OBJECT

    public:
        explicit MetricsCollectorWorker(const QString& nodeName, QObject* parent = nullptr);
        ~MetricsCollectorWorker();

    public slots:
        void startCollecting(int intervalMs);
        void stopCollecting();
        void collectOnce(); // для принудительного сбора

        signals:
            void snapshotCollected(const rmm::shared::MetricsSnapshot& snapshot);
        void error(const QString& message);

    private:
        void runCollection();

        QString m_nodeName;
        std::unique_ptr<MetricsCollector> m_collector;
        std::atomic<bool> m_running{false};
        int m_intervalMs{5000};
    };

} // namespace rmm::client::business
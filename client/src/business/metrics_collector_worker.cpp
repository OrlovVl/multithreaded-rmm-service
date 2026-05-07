#include "rmm/client/business/metrics_collector_worker.hpp"
#include "rmm/client/business/metrics_collector.hpp"

#include <QThread>
#include <QTimer>
#include <QCoreApplication>

namespace rmm::client::business {

MetricsCollectorWorker::MetricsCollectorWorker(const QString& nodeName, QObject* parent)
    : QObject(parent)
    , m_nodeName(nodeName)
    , m_collector(std::make_unique<MetricsCollector>())
{
}

MetricsCollectorWorker::~MetricsCollectorWorker()
{
    stopCollecting();
}

void MetricsCollectorWorker::startCollecting(int intervalMs)
{
    m_intervalMs = intervalMs;
    m_running = true;
    runCollection();
}

void MetricsCollectorWorker::stopCollecting()
{
    m_running = false;
}

void MetricsCollectorWorker::collectOnce()
{
    if (!m_running)
        return;

    try {
        auto snapshot = m_collector->collect(m_nodeName.toStdString());
        emit snapshotCollected(snapshot);
    } catch (const std::exception& e) {
        emit error(QString("Collection error: %1").arg(e.what()));
    }

    if (m_running)
    {
        QTimer::singleShot(m_intervalMs, this, &MetricsCollectorWorker::collectOnce);
    }
}

void MetricsCollectorWorker::runCollection()
{
    QTimer::singleShot(0, this, &MetricsCollectorWorker::collectOnce);
}

} // namespace rmm::client::business
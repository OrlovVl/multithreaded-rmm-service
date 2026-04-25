#include <QtTest/QtTest>

#include "rmm/client/data/local_metrics_store.hpp"

class LocalMetricsStoreTest final : public QObject
{
    Q_OBJECT

private slots:
    void insertPeekErase()
    {
        rmm::client::data::LocalMetricsStore store("test_client_outbox.sqlite3");
        const auto id = store.enqueue(R"({"nodeName":"n1","cpuUsage":11.5})");
        QCOMPARE(static_cast<qint64>(id > 0 ? 1 : 0), static_cast<qint64>(1));

        const auto item = store.peekOldest();
        QVERIFY(item.has_value());
        QCOMPARE(item->id, id);
        QVERIFY(item->payloadJson.find("cpuUsage") != std::string::npos);

        store.erase(id);
        QVERIFY(!store.peekOldest().has_value());
    }
};

QTEST_MAIN(LocalMetricsStoreTest)
#include "test_local_metrics_store.moc"
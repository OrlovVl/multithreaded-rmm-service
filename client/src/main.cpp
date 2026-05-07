#include "rmm/client/business/sync_service.hpp"
#include "rmm/client/data/local_metrics_store.hpp"
#include "rmm/client/network/client_channel.hpp"
#include "rmm/client/ui/main_window.hpp"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    qRegisterMetaType<rmm::shared::MetricsSnapshot>("rmm::shared::MetricsSnapshot");

    rmm::client::network::ClientChannel channel;
    rmm::client::data::LocalMetricsStore store("rmm_client.sqlite3");
    rmm::client::business::SyncService syncService(&channel, &store);

    QObject::connect(&channel, &rmm::client::network::ClientChannel::ackReceived,
                     &syncService, &rmm::client::business::SyncService::onAckReceived);
    QObject::connect(&channel, &rmm::client::network::ClientChannel::serverCommandReceived,
                     &syncService, &rmm::client::business::SyncService::onServerCommand);

    rmm::client::ui::MainWindow window(&channel, &syncService);
    window.show();

    return app.exec();
}
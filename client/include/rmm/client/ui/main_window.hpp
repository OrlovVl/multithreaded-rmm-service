#pragma once

#include <QCloseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTextEdit>
#include <QTabWidget>

#include "rmm/shared/models.hpp"

namespace rmm::client::network {
class ClientChannel;
}
namespace rmm::client::business {
class SyncService;
}

namespace rmm::client::ui {

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(network::ClientChannel* channel,
                        business::SyncService* syncService,
                        QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onConnectClicked();
    void onLoginClicked();
    void onRefreshClicked();
    void onConnectedChanged(bool connected);
    void onSnapshotCollected(const rmm::shared::MetricsSnapshot& snapshot);
    void onLogMessage(const QString& text);
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    // Админские слоты
    void onRequestNodesList();
    void onNodesListReceived(const QStringList& nodes);
    void onNodeSelected();
    void onRequestNodeMetrics();
    void onNodeMetricsReceived(const rmm::shared::MetricsSnapshot& snapshot);
    void onSendCommand();

private:
    void setupUi();
    void setupTray();
    void updateProcesses(const rmm::shared::MetricsSnapshot& snapshot);
    void setupAdminTab();

    network::ClientChannel* m_channel;
    business::SyncService* m_syncService;

    // Основные виджеты
    QLineEdit* m_hostEdit{};
    QLineEdit* m_portEdit{};
    QLineEdit* m_nodeEdit{};
    QLineEdit* m_userEdit{};
    QLineEdit* m_passEdit{};
    QPushButton* m_connectButton{};
    QPushButton* m_loginButton{};
    QPushButton* m_refreshButton{};

    QLabel* m_connectionLabel{};
    QLabel* m_cpuLabel{};
    QLabel* m_ramLabel{};
    QLabel* m_diskLabel{};
    QLabel* m_tempLabel{};
    QLabel* m_smartLabel{};
    QListWidget* m_processList{};
    QTextEdit* m_log{};

    QTabWidget* m_tabWidget{};
    QWidget* m_adminTab{};
    QTableWidget* m_nodesTable{};
    QPushButton* m_refreshNodesBtn{};
    QPushButton* m_getMetricsBtn{};
    QPushButton* m_sendCmdBtn{};
    QLineEdit* m_cmdEdit{};
    QTextEdit* m_adminMetricsDisplay{};

    QSystemTrayIcon* m_tray{};
    bool m_hideToTray{true};
};

} // namespace rmm::client::ui
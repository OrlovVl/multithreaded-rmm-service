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
#include <QStackedWidget>

#include "rmm/shared/models.hpp"

namespace rmm::client::network {
class ClientChannel;
}
namespace rmm::client::business {
class SyncService;
}

namespace rmm::client::ui {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(network::ClientChannel* channel,
                        business::SyncService* syncService,
                        QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onConnectClicked();
    void onDisconnectClicked();
    void onLoginClicked();
    void onRegisterClicked();
    void onLogoutClicked();
    void onConnectedChanged(bool connected);
    void onSnapshotCollected(const rmm::shared::MetricsSnapshot& snapshot);
    void onLogMessage(const QString& text);
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onLoginResponse(const QString& status, const QString& role,
                         const QString& nodeName, qint64 userId);
    void onRegisterResponse(const QString& status, const QString& message,
                            const QString& nodeName, qint64 userId);
    // Админские слоты
    void onRequestNodesList();
    void onNodesListReceived(const QStringList& nodes);
    void onNodeSelected();
    void onRequestNodeMetrics();
    void onNodeMetricsReceived(const rmm::shared::MetricsSnapshot& snapshot);
    void onSendRefreshCommand();
    void onSendShutdownCommand();
    void onSendRestartCommand();
    void onSendCustomCommand();

private:
    void setupUi();
    void setupTray();
    void updateProcesses(const rmm::shared::MetricsSnapshot& snapshot);
    void setupAdminTab();
    void updateConnectionState(bool connected);
    void updateAdminState(bool isAdmin);

    network::ClientChannel* m_channel;
    business::SyncService* m_syncService;

    QLineEdit* m_hostEdit{};
    QLineEdit* m_portEdit{};
    QPushButton* m_connectButton{};
    QPushButton* m_disconnectButton{};

    QLineEdit* m_userEdit{};
    QLineEdit* m_passEdit{};
    QPushButton* m_loginButton{};
    QPushButton* m_registerButton{};
    QPushButton* m_logoutButton{};

    QLabel* m_connectionLabel{};
    QLabel* m_roleLabel{};
    QLabel* m_nodeLabel{};

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
    QPushButton* m_refreshCmdBtn{};
    QPushButton* m_shutdownCmdBtn{};
    QPushButton* m_restartCmdBtn{};
    QLineEdit* m_customCmdEdit{};
    QPushButton* m_sendCustomCmdBtn{};
    QTextEdit* m_adminMetricsDisplay{};

    QSystemTrayIcon* m_tray{};
    bool m_hideToTray{true};
    bool m_connected{false};
    bool m_isAdmin{false};
};

} // namespace rmm::client::ui
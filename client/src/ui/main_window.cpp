#include "rmm/client/ui/main_window.hpp"
#include "rmm/client/business/sync_service.hpp"
#include "rmm/client/network/client_channel.hpp"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>

namespace rmm::client::ui {

MainWindow::MainWindow(network::ClientChannel* channel,
                       business::SyncService* syncService,
                       QWidget* parent)
    : QMainWindow(parent), m_channel(channel), m_syncService(syncService) {
    setupUi();
    setupTray();
    setupAdminTab();

    connect(m_channel, &network::ClientChannel::connectedChanged, this, &MainWindow::onConnectedChanged);
    connect(m_syncService, &business::SyncService::snapshotCollected, this, &MainWindow::onSnapshotCollected);
    connect(m_syncService, &business::SyncService::logMessage, this, &MainWindow::onLogMessage);
    connect(m_channel, &network::ClientChannel::logMessage, this, &MainWindow::onLogMessage);
    connect(m_syncService, &business::SyncService::nodesListReceived, this, &MainWindow::onNodesListReceived);
    connect(m_syncService, &business::SyncService::nodeMetricsReceived, this, &MainWindow::onNodeMetricsReceived);

    connect(m_syncService, &business::SyncService::loginResult, this, &MainWindow::onLoginResponse);
    connect(m_syncService, &business::SyncService::registerResult, this, &MainWindow::onRegisterResponse);

    setWindowTitle("RMM Client");
    resize(980, 720);
    updateConnectionState(false);
    updateAdminState(false);
}

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    // --- Секция подключения ---
    auto* connectionGroup = new QGroupBox("Connection", this);
    auto* connectionLayout = new QGridLayout(connectionGroup);

    m_hostEdit = new QLineEdit("127.0.0.1", this);
    m_portEdit = new QLineEdit("5555", this);
    m_connectButton = new QPushButton("Connect", this);
    m_disconnectButton = new QPushButton("Disconnect", this);

    connectionLayout->addWidget(new QLabel("Host:"), 0, 0);
    connectionLayout->addWidget(m_hostEdit, 0, 1);
    connectionLayout->addWidget(new QLabel("Port:"), 0, 2);
    connectionLayout->addWidget(m_portEdit, 0, 3);
    connectionLayout->addWidget(m_connectButton, 1, 0);
    connectionLayout->addWidget(m_disconnectButton, 1, 1);

    // --- Секция авторизации ---
    auto* authGroup = new QGroupBox("Authentication", this);
    auto* authLayout = new QGridLayout(authGroup);

    m_userEdit = new QLineEdit("admin", this);
    m_passEdit = new QLineEdit("admin", this);
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_loginButton = new QPushButton("Login", this);
    m_registerButton = new QPushButton("Register", this);
    m_logoutButton = new QPushButton("Logout", this);

    authLayout->addWidget(new QLabel("Username:"), 0, 0);
    authLayout->addWidget(m_userEdit, 0, 1);
    authLayout->addWidget(new QLabel("Password:"), 0, 2);
    authLayout->addWidget(m_passEdit, 0, 3);
    authLayout->addWidget(m_loginButton, 1, 0);
    authLayout->addWidget(m_registerButton, 1, 1);
    authLayout->addWidget(m_logoutButton, 1, 2);

    // --- Статус ---
    m_connectionLabel = new QLabel("Disconnected", this);
    m_connectionLabel->setStyleSheet("font-weight: bold;");
    m_roleLabel = new QLabel("Role: none", this);
    m_nodeLabel = new QLabel("Node: -", this);

    auto* statusRow = new QHBoxLayout();
    statusRow->addWidget(m_connectionLabel);
    statusRow->addWidget(m_roleLabel);
    statusRow->addWidget(m_nodeLabel);
    statusRow->addStretch();

    // --- Метрики ---
    m_cpuLabel = new QLabel("CPU: 0%", this);
    m_ramLabel = new QLabel("RAM: 0%", this);
    m_diskLabel = new QLabel("Disk: 0%", this);
    m_tempLabel = new QLabel("Temp: 0 C", this);
    m_smartLabel = new QLabel("SMART: OK", this);

    auto* metricsLayout = new QHBoxLayout();
    metricsLayout->addWidget(m_cpuLabel);
    metricsLayout->addWidget(m_ramLabel);
    metricsLayout->addWidget(m_diskLabel);
    metricsLayout->addWidget(m_tempLabel);
    metricsLayout->addWidget(m_smartLabel);
    metricsLayout->addStretch();

    m_processList = new QListWidget(this);
    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);

    auto* listsLayout = new QHBoxLayout();
    listsLayout->addWidget(m_processList, 2);
    listsLayout->addWidget(m_log, 1);

    m_tabWidget = new QTabWidget(this);
    auto* localTab = new QWidget();
    auto* localTabLayout = new QVBoxLayout(localTab);
    localTabLayout->addWidget(connectionGroup);
    localTabLayout->addWidget(authGroup);
    localTabLayout->addLayout(statusRow);
    localTabLayout->addLayout(metricsLayout);
    localTabLayout->addLayout(listsLayout);
    m_tabWidget->addTab(localTab, "Local");

    root->addWidget(m_tabWidget);
    setCentralWidget(central);

    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_disconnectButton, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
    connect(m_loginButton, &QPushButton::clicked, this, &MainWindow::onLoginClicked);
    connect(m_registerButton, &QPushButton::clicked, this, &MainWindow::onRegisterClicked);
    connect(m_logoutButton, &QPushButton::clicked, this, &MainWindow::onLogoutClicked);
}

void MainWindow::setupAdminTab() {
    m_adminTab = new QWidget();
    auto* layout = new QVBoxLayout(m_adminTab);

    auto* btnLayout = new QHBoxLayout();
    m_refreshNodesBtn = new QPushButton("Refresh Nodes");
    m_getMetricsBtn = new QPushButton("Get Metrics");
    btnLayout->addWidget(m_refreshNodesBtn);
    btnLayout->addWidget(m_getMetricsBtn);
    layout->addLayout(btnLayout);

    auto* cmdGroup = new QGroupBox("Commands", this);
    auto* cmdLayout = new QVBoxLayout(cmdGroup);
    auto* cmdRow1 = new QHBoxLayout();
    m_refreshCmdBtn = new QPushButton("Refresh All");
    m_shutdownCmdBtn = new QPushButton("Shutdown All");
    m_restartCmdBtn = new QPushButton("Restart All");
    cmdRow1->addWidget(m_refreshCmdBtn);
    cmdRow1->addWidget(m_shutdownCmdBtn);
    cmdRow1->addWidget(m_restartCmdBtn);
    auto* cmdRow2 = new QHBoxLayout();
    m_customCmdEdit = new QLineEdit();
    m_customCmdEdit->setPlaceholderText("Custom command (e.g., execute notepad)");
    m_sendCustomCmdBtn = new QPushButton("Send Custom");
    cmdRow2->addWidget(m_customCmdEdit);
    cmdRow2->addWidget(m_sendCustomCmdBtn);
    cmdLayout->addLayout(cmdRow1);
    cmdLayout->addLayout(cmdRow2);
    layout->addWidget(cmdGroup);

    m_nodesTable = new QTableWidget(0, 1, this);
    m_nodesTable->setHorizontalHeaderLabels({"Node Name"});
    m_nodesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(m_nodesTable, 1);

    m_adminMetricsDisplay = new QTextEdit();
    m_adminMetricsDisplay->setReadOnly(true);
    layout->addWidget(m_adminMetricsDisplay, 1);

    m_tabWidget->addTab(m_adminTab, "Admin");
    m_tabWidget->setTabEnabled(1, false);

    connect(m_refreshNodesBtn, &QPushButton::clicked, this, &MainWindow::onRequestNodesList);
    connect(m_getMetricsBtn, &QPushButton::clicked, this, &MainWindow::onRequestNodeMetrics);
    connect(m_refreshCmdBtn, &QPushButton::clicked, this, &MainWindow::onSendRefreshCommand);
    connect(m_shutdownCmdBtn, &QPushButton::clicked, this, &MainWindow::onSendShutdownCommand);
    connect(m_restartCmdBtn, &QPushButton::clicked, this, &MainWindow::onSendRestartCommand);
    connect(m_sendCustomCmdBtn, &QPushButton::clicked, this, &MainWindow::onSendCustomCommand);
    connect(m_nodesTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::onNodeSelected);
}

void MainWindow::setupTray() {
    m_tray = new QSystemTrayIcon(style()->standardIcon(QStyle::SP_ComputerIcon), this);
    auto* menu = new QMenu(this);
    auto* showAction = menu->addAction("Show");
    auto* quitAction = menu->addAction("Quit");
    connect(showAction, &QAction::triggered, this, [this]() { showNormal(); raise(); activateWindow(); });
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    m_tray->setContextMenu(menu);
    m_tray->show();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_hideToTray) {
        hide();
        m_tray->showMessage("RMM Client", "Application was minimized to tray", QSystemTrayIcon::Information, 2000);
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::updateConnectionState(bool connected) {
    m_connected = connected;
    m_connectButton->setEnabled(!connected);
    m_disconnectButton->setEnabled(connected);
    m_loginButton->setEnabled(connected);
    m_registerButton->setEnabled(connected);
    m_logoutButton->setEnabled(connected && (m_isAdmin || m_roleLabel->text().contains("user")));
    m_connectionLabel->setText(connected ? "Connected" : "Disconnected");
    if (!connected) updateAdminState(false);
}

void MainWindow::updateAdminState(bool isAdmin) {
    m_isAdmin = isAdmin;
    m_tabWidget->setTabEnabled(1, isAdmin);
    m_logoutButton->setEnabled(m_connected && !m_roleLabel->text().isEmpty());
}

void MainWindow::onConnectClicked() {
    m_channel->connectToServer(m_hostEdit->text(), static_cast<quint16>(m_portEdit->text().toUShort()));
}

void MainWindow::onDisconnectClicked() {
    m_channel->disconnectFromServer();
    m_syncService->stop();
    m_roleLabel->setText("Role: none");
    m_nodeLabel->setText("Node: -");
    updateAdminState(false);
}

void MainWindow::onLoginClicked() {
    QJsonObject obj{{"username", m_userEdit->text()}, {"password", m_passEdit->text()}};
    m_channel->sendWireMessage("LOGIN", QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
}

void MainWindow::onRegisterClicked() {
    // Диалог регистрации
    QDialog dialog(this);
    dialog.setWindowTitle("Register new user");
    QFormLayout form(&dialog);
    QLineEdit* userEdit = new QLineEdit(&dialog);
    QLineEdit* passEdit = new QLineEdit(&dialog);
    passEdit->setEchoMode(QLineEdit::Password);
    QLineEdit* confirmEdit = new QLineEdit(&dialog);
    confirmEdit->setEchoMode(QLineEdit::Password);
    form.addRow("Username:", userEdit);
    form.addRow("Password:", passEdit);
    form.addRow("Confirm:", confirmEdit);

    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        QString user = userEdit->text().trimmed();
        QString pass = passEdit->text();
        QString confirm = confirmEdit->text();
        if (user.isEmpty() || pass.isEmpty()) {
            QMessageBox::warning(this, "Error", "Username and password cannot be empty.");
            return;
        }
        if (pass != confirm) {
            QMessageBox::warning(this, "Error", "Passwords do not match.");
            return;
        }
        QJsonObject obj{{"username", user}, {"password", pass}};
        m_channel->sendWireMessage("REGISTER", QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    }
}

void MainWindow::onLogoutClicked() {
    m_syncService->logout();
    updateAdminState(false);
    m_roleLabel->setText("Role: none");
    m_nodeLabel->setText("Node: -");
}

void MainWindow::onConnectedChanged(bool connected) {
    updateConnectionState(connected);
}

void MainWindow::onLoginResponse(const QString& status, const QString& role,
                                 const QString& nodeName, qint64 userId) {
    if (status != "ok") {
        onLogMessage("Login failed");
        return;
    }
    bool isAdmin = (role == "admin");
    updateAdminState(isAdmin);
    m_roleLabel->setText(QString("Role: %1").arg(role));
    m_nodeLabel->setText(QString("Node: %1").arg(nodeName));
    onLogMessage(QString("Logged in as %1 on node %2").arg(role, nodeName));
}

void MainWindow::onRegisterResponse(const QString& status, const QString& message,
                                    const QString& nodeName, qint64 userId) {
    if (status == "ok") {
        QMessageBox::information(this, "Registration successful",
                                 QString("Registered as %1. You are now logged in.").arg(nodeName));
        // После успешной регистрации сервер уже установил m_isAdmin = false,
        // автоматически логин: обновляем UI как при логине
        updateAdminState(false);
        m_roleLabel->setText("Role: user");
        m_nodeLabel->setText(QString("Node: %1").arg(nodeName));
        onLogMessage("Registered and logged in automatically");
    } else {
        QMessageBox::warning(this, "Registration failed", message.isEmpty() ? "Unknown error" : message);
    }
}

void MainWindow::onSnapshotCollected(const rmm::shared::MetricsSnapshot& snapshot) {
    m_cpuLabel->setText(QString("CPU: %1%").arg(snapshot.cpuUsage, 0, 'f', 1));
    m_ramLabel->setText(QString("RAM: %1%").arg(snapshot.ramUsage, 0, 'f', 1));
    m_diskLabel->setText(QString("Disk free: %1%").arg(snapshot.diskFreePercent, 0, 'f', 1));
    m_tempLabel->setText(QString("Temp: %1 C").arg(snapshot.temperatureC, 0, 'f', 1));
    m_smartLabel->setText(QString("SMART: %1").arg(snapshot.smartPredictFailure ? "FAIL" : "OK"));
    updateProcesses(snapshot);
}

void MainWindow::updateProcesses(const rmm::shared::MetricsSnapshot& snapshot) {
    m_processList->clear();
    for (const auto& p : snapshot.processes)
        m_processList->addItem(QString::number(p.pid) + "  " + QString::fromStdString(p.name));
}

void MainWindow::onLogMessage(const QString& text) { m_log->append(text); }

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick) { showNormal(); raise(); activateWindow(); }
}

// Админские слоты (без изменений)
void MainWindow::onRequestNodesList() { m_syncService->requestNodesList(); }
void MainWindow::onNodesListReceived(const QStringList& nodes) {
    m_nodesTable->setRowCount(nodes.size());
    for (int i = 0; i < nodes.size(); ++i)
        m_nodesTable->setItem(i, 0, new QTableWidgetItem(nodes[i]));
    onLogMessage("Nodes list updated");
}
void MainWindow::onNodeSelected() {}
void MainWindow::onRequestNodeMetrics() {
    auto selected = m_nodesTable->selectedItems();
    if (selected.isEmpty()) { onLogMessage("No node selected"); return; }
    m_syncService->requestNodeMetrics(selected.first()->text());
}
void MainWindow::onNodeMetricsReceived(const rmm::shared::MetricsSnapshot& snapshot) {
    QString text = QString("Node: %1\nTime: %2\nCPU: %3%\nRAM: %4%\nDisk: %5% (%6 GB)\nTemp: %7 C\nSMART: %8\nProcesses:\n")
                       .arg(QString::fromStdString(snapshot.nodeName))
                       .arg(QString::fromStdString(snapshot.timestampUtc))
                       .arg(snapshot.cpuUsage).arg(snapshot.ramUsage)
                       .arg(snapshot.diskFreePercent).arg(snapshot.diskFreeGb)
                       .arg(snapshot.temperatureC)
                       .arg(snapshot.smartPredictFailure ? "FAIL" : "OK");
    for (const auto& p : snapshot.processes)
        text += QString("  %1 %2\n").arg(p.pid).arg(QString::fromStdString(p.name));
    m_adminMetricsDisplay->setText(text);
}
void MainWindow::onSendRefreshCommand() {
    QJsonObject cmd{{"action", "refresh"}};
    m_channel->sendWireMessage("COMMAND", QString::fromUtf8(QJsonDocument(cmd).toJson(QJsonDocument::Compact)));
    onLogMessage("Refresh command sent");
}
void MainWindow::onSendShutdownCommand() {
    QJsonObject cmd{{"action", "shutdown"}};
    m_channel->sendWireMessage("COMMAND", QString::fromUtf8(QJsonDocument(cmd).toJson(QJsonDocument::Compact)));
    onLogMessage("Shutdown command sent");
}
void MainWindow::onSendRestartCommand() {
    QJsonObject cmd{{"action", "restart"}};
    m_channel->sendWireMessage("COMMAND", QString::fromUtf8(QJsonDocument(cmd).toJson(QJsonDocument::Compact)));
    onLogMessage("Restart command sent");
}
void MainWindow::onSendCustomCommand() {
    QString text = m_customCmdEdit->text().trimmed();
    if (text.isEmpty()) return;
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        QJsonObject cmd;
        cmd["action"] = "execute";
        cmd["command"] = text;
        m_channel->sendWireMessage("COMMAND", QString::fromUtf8(QJsonDocument(cmd).toJson(QJsonDocument::Compact)));
    } else {
        m_channel->sendWireMessage("COMMAND", text);
    }
    onLogMessage("Custom command sent: " + text);
}

} // namespace rmm::client::ui
#include "rmm/client/ui/main_window.hpp"

#include "rmm/client/business/sync_service.hpp"
#include "rmm/client/network/client_channel.hpp"

#include <QApplication>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QStyle>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>

namespace rmm::client::ui {

MainWindow::MainWindow(network::ClientChannel* channel,
                       business::SyncService* syncService,
                       QWidget* parent)
    : QMainWindow(parent)
    , m_channel(channel)
    , m_syncService(syncService)
{
    setupUi();
    setupTray();
    setupAdminTab();

    connect(m_channel, &network::ClientChannel::connectedChanged, this, &MainWindow::onConnectedChanged);
    connect(m_syncService, &business::SyncService::snapshotCollected, this, &MainWindow::onSnapshotCollected);
    connect(m_syncService, &business::SyncService::logMessage, this, &MainWindow::onLogMessage);
    connect(m_channel, &network::ClientChannel::logMessage, this, &MainWindow::onLogMessage);
    connect(m_syncService, &business::SyncService::nodesListReceived, this, &MainWindow::onNodesListReceived);
    connect(m_syncService, &business::SyncService::nodeMetricsReceived, this, &MainWindow::onNodeMetricsReceived);

    setWindowTitle("RMM Client");
    resize(980, 720);
}

void MainWindow::setupUi()
{
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    auto* connectionRow = new QGridLayout();
    m_hostEdit = new QLineEdit("127.0.0.1", this);
    m_portEdit = new QLineEdit("5555", this);
    m_nodeEdit = new QLineEdit("node-1", this);
    m_userEdit = new QLineEdit("admin", this);
    m_passEdit = new QLineEdit("admin", this);
    m_passEdit->setEchoMode(QLineEdit::Password);

    connectionRow->addWidget(new QLabel("Host"), 0, 0);
    connectionRow->addWidget(m_hostEdit, 0, 1);
    connectionRow->addWidget(new QLabel("Port"), 0, 2);
    connectionRow->addWidget(m_portEdit, 0, 3);
    connectionRow->addWidget(new QLabel("Node"), 1, 0);
    connectionRow->addWidget(m_nodeEdit, 1, 1);
    connectionRow->addWidget(new QLabel("User"), 1, 2);
    connectionRow->addWidget(m_userEdit, 1, 3);
    connectionRow->addWidget(new QLabel("Pass"), 1, 4);
    connectionRow->addWidget(m_passEdit, 1, 5);

    m_connectButton = new QPushButton("Connect", this);
    m_loginButton = new QPushButton("Admin login", this);
    m_refreshButton = new QPushButton("Broadcast refresh", this);

    auto* buttons = new QHBoxLayout();
    buttons->addWidget(m_connectButton);
    buttons->addWidget(m_loginButton);
    buttons->addWidget(m_refreshButton);
    buttons->addStretch();

    m_connectionLabel = new QLabel("Disconnected", this);
    m_cpuLabel = new QLabel("CPU: 0%", this);
    m_ramLabel = new QLabel("RAM: 0%", this);
    m_diskLabel = new QLabel("Disk: 0%", this);
    m_tempLabel = new QLabel("Temp: 0 C", this);
    m_smartLabel = new QLabel("SMART: OK", this);

    auto* metricsRow = new QHBoxLayout();
    metricsRow->addWidget(m_connectionLabel);
    metricsRow->addWidget(m_cpuLabel);
    metricsRow->addWidget(m_ramLabel);
    metricsRow->addWidget(m_diskLabel);
    metricsRow->addWidget(m_tempLabel);
    metricsRow->addWidget(m_smartLabel);
    metricsRow->addStretch();

    m_processList = new QListWidget(this);
    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);

    auto* lists = new QHBoxLayout();
    lists->addWidget(m_processList, 2);
    lists->addWidget(m_log, 1);

    m_tabWidget = new QTabWidget(this);
    auto* mainTab = new QWidget();
    auto* mainTabLayout = new QVBoxLayout(mainTab);
    mainTabLayout->addLayout(connectionRow);
    mainTabLayout->addLayout(buttons);
    mainTabLayout->addLayout(metricsRow);
    mainTabLayout->addLayout(lists);
    m_tabWidget->addTab(mainTab, "Local");

    root->addWidget(m_tabWidget);
    setCentralWidget(central);

    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_loginButton, &QPushButton::clicked, this, &MainWindow::onLoginClicked);
    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);
}

void MainWindow::setupAdminTab()
{
    m_adminTab = new QWidget();
    auto* layout = new QVBoxLayout(m_adminTab);

    // Кнопки управления
    auto* btnLayout = new QHBoxLayout();
    m_refreshNodesBtn = new QPushButton("Refresh Nodes");
    m_getMetricsBtn = new QPushButton("Get Metrics");
    m_sendCmdBtn = new QPushButton("Send Command");
    m_cmdEdit = new QLineEdit();
    m_cmdEdit->setPlaceholderText("Command JSON (e.g., {\"action\":\"refresh\"})");
    btnLayout->addWidget(m_refreshNodesBtn);
    btnLayout->addWidget(m_getMetricsBtn);
    btnLayout->addWidget(m_cmdEdit);
    btnLayout->addWidget(m_sendCmdBtn);
    layout->addLayout(btnLayout);

    // Таблица узлов
    m_nodesTable = new QTableWidget(0, 1, this);
    m_nodesTable->setHorizontalHeaderLabels({"Node Name"});
    m_nodesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(m_nodesTable, 1);

    // Область отображения метрик
    m_adminMetricsDisplay = new QTextEdit();
    m_adminMetricsDisplay->setReadOnly(true);
    layout->addWidget(m_adminMetricsDisplay, 1);

    m_tabWidget->addTab(m_adminTab, "Admin");

    connect(m_refreshNodesBtn, &QPushButton::clicked, this, &MainWindow::onRequestNodesList);
    connect(m_getMetricsBtn, &QPushButton::clicked, this, &MainWindow::onRequestNodeMetrics);
    connect(m_sendCmdBtn, &QPushButton::clicked, this, &MainWindow::onSendCommand);
    connect(m_nodesTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::onNodeSelected);
}

void MainWindow::setupTray()
{
    m_tray = new QSystemTrayIcon(style()->standardIcon(QStyle::SP_ComputerIcon), this);
    auto* menu = new QMenu(this);

    auto* showAction = menu->addAction("Show");
    auto* quitAction = menu->addAction("Quit");

    connect(showAction, &QAction::triggered, this, [this] {
        showNormal();
        raise();
        activateWindow();
    });

    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);

    m_tray->setContextMenu(menu);
    m_tray->show();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_hideToTray)
    {
        hide();
        m_tray->showMessage("RMM Client", "Application was minimized to tray", QSystemTrayIcon::Information, 2000);
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::onConnectClicked()
{
    m_channel->connectToServer(m_hostEdit->text(), static_cast<quint16>(m_portEdit->text().toUShort()));
}

void MainWindow::onLoginClicked()
{
    const QJsonObject hello{{"nodeName", m_nodeEdit->text()}};
    m_channel->sendWireMessage("HELLO", QString::fromUtf8(QJsonDocument(hello).toJson(QJsonDocument::Compact)));

    const QJsonObject auth{{"username", m_userEdit->text()}, {"password", m_passEdit->text()}};
    m_channel->sendWireMessage("AUTH", QString::fromUtf8(QJsonDocument(auth).toJson(QJsonDocument::Compact)));

    m_syncService->start(m_nodeEdit->text(), 5000);
}

void MainWindow::onRefreshClicked()
{
    const QJsonObject cmd{{"action", "refresh"}};
    m_channel->sendWireMessage("COMMAND", QString::fromUtf8(QJsonDocument(cmd).toJson(QJsonDocument::Compact)));
}

void MainWindow::onConnectedChanged(bool connected)
{
    m_connectionLabel->setText(connected ? "Connected" : "Disconnected");
}

void MainWindow::onSnapshotCollected(const rmm::shared::MetricsSnapshot& snapshot)
{
    m_cpuLabel->setText(QString("CPU: %1%").arg(snapshot.cpuUsage, 0, 'f', 1));
    m_ramLabel->setText(QString("RAM: %1%").arg(snapshot.ramUsage, 0, 'f', 1));
    m_diskLabel->setText(QString("Disk free: %1%").arg(snapshot.diskFreePercent, 0, 'f', 1));
    m_tempLabel->setText(QString("Temp: %1 C").arg(snapshot.temperatureC, 0, 'f', 1));
    m_smartLabel->setText(QString("SMART: %1").arg(snapshot.smartPredictFailure ? "FAIL" : "OK"));
    updateProcesses(snapshot);
}

void MainWindow::updateProcesses(const rmm::shared::MetricsSnapshot& snapshot)
{
    m_processList->clear();
    for (const auto& process : snapshot.processes)
        m_processList->addItem(QString::number(process.pid) + "  " + QString::fromStdString(process.name));
}

void MainWindow::onLogMessage(const QString& text)
{
    m_log->append(text);
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick)
    {
        showNormal();
        raise();
        activateWindow();
    }
}

// Админские слоты
void MainWindow::onRequestNodesList()
{
    m_syncService->requestNodesList();
}

void MainWindow::onNodesListReceived(const QStringList& nodes)
{
    m_nodesTable->setRowCount(nodes.size());
    for (int i = 0; i < nodes.size(); ++i)
        m_nodesTable->setItem(i, 0, new QTableWidgetItem(nodes[i]));
    onLogMessage("Nodes list updated");
}

void MainWindow::onNodeSelected()
{
    // Можно ничего не делать, или предзаполнить
}

void MainWindow::onRequestNodeMetrics()
{
    auto selected = m_nodesTable->selectedItems();
    if (selected.isEmpty())
    {
        onLogMessage("No node selected");
        return;
    }
    QString nodeName = selected.first()->text();
    m_syncService->requestNodeMetrics(nodeName);
}

void MainWindow::onNodeMetricsReceived(const rmm::shared::MetricsSnapshot& snapshot)
{
    QString text = QString("Node: %1\nTime: %2\nCPU: %3%\nRAM: %4%\nDisk free: %5% (%6 GB)\nTemp: %7 C\nSMART: %8\nProcesses:\n")
                       .arg(QString::fromStdString(snapshot.nodeName))
                       .arg(QString::fromStdString(snapshot.timestampUtc))
                       .arg(snapshot.cpuUsage)
                       .arg(snapshot.ramUsage)
                       .arg(snapshot.diskFreePercent)
                       .arg(snapshot.diskFreeGb)
                       .arg(snapshot.temperatureC)
                       .arg(snapshot.smartPredictFailure ? "FAIL" : "OK");
    for (const auto& p : snapshot.processes)
        text += QString("  %1 %2\n").arg(p.pid).arg(QString::fromStdString(p.name));
    m_adminMetricsDisplay->setText(text);
}

void MainWindow::onSendCommand()
{
    QString cmd = m_cmdEdit->text();
    if (cmd.isEmpty())
        return;
    // Отправляем команду через COMMAND (для админа)
    m_channel->sendWireMessage("COMMAND", cmd);
    onLogMessage("Command sent: " + cmd);
}

} // namespace rmm::client::ui
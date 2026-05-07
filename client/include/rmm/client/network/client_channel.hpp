#pragma once

#include <QObject>
#include <QTcpSocket>

#include "rmm/shared/models.hpp"

namespace rmm::client::network {

    class ClientChannel final : public QObject
    {
        Q_OBJECT

    public:
        explicit ClientChannel(QObject* parent = nullptr);

        void connectToServer(const QString& host, quint16 port);
        void disconnectFromServer();
        void sendWireMessage(const QString& type, const QString& payload);

        bool isConnected() const noexcept;

        signals:
            void connectedChanged(bool connected);
        void messageReceived(const QString& type, const QString& payload);
        void ackReceived(qint64 localId);
        void serverCommandReceived(const QString& payload);
        void logMessage(const QString& text);

    private slots:
        void onConnected();
        void onDisconnected();
        void onReadyRead();
        void onErrorOccurred(QAbstractSocket::SocketError socketError);

    private:
        QTcpSocket m_socket;
        QByteArray m_buffer;
    };

} // namespace rmm::client::network
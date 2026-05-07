#include "rmm/client/network/client_channel.hpp"

#include "rmm/shared/protocol.hpp"

#include <QJsonDocument>
#include <QJsonObject>

namespace rmm::client::network {

ClientChannel::ClientChannel(QObject* parent)
    : QObject(parent)
{
    connect(&m_socket, &QTcpSocket::connected, this, &ClientChannel::onConnected);
    connect(&m_socket, &QTcpSocket::disconnected, this, &ClientChannel::onDisconnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &ClientChannel::onReadyRead);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &ClientChannel::onErrorOccurred);
}

void ClientChannel::connectToServer(const QString& host, quint16 port)
{
    m_socket.connectToHost(host, port);
}

void ClientChannel::disconnectFromServer()
{
    m_socket.disconnectFromHost();
}

void ClientChannel::sendWireMessage(const QString& type, const QString& payload)
{
    if (!m_socket.isOpen())
    {
        return;
    }

    const auto line = rmm::shared::encodeWireMessage({type.toStdString(), payload.toStdString()});
    m_socket.write(QByteArray::fromStdString(line));
    m_socket.flush();
}

bool ClientChannel::isConnected() const noexcept
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

void ClientChannel::onConnected()
{
    emit connectedChanged(true);
    emit logMessage("Connected to server");
}

void ClientChannel::onDisconnected()
{
    emit connectedChanged(false);
    emit logMessage("Disconnected from server");
}

void ClientChannel::onReadyRead()
{
    m_buffer.append(m_socket.readAll());

    while (true)
    {
        const int newline = m_buffer.indexOf('\n');
        if (newline < 0)
        {
            break;
        }

        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);

        const auto message = rmm::shared::decodeWireMessage(line.toStdString());
        if (!message)
        {
            continue;
        }

        const QString type = QString::fromStdString(message->type);
        const QString payload = QString::fromStdString(message->payload);
        emit messageReceived(type, payload);

        if (type == "ACK")
        {
            const auto doc = QJsonDocument::fromJson(payload.toUtf8());
            const qint64 localId = static_cast<qint64>(doc.object().value("localId").toDouble(0.0));
            emit ackReceived(localId);
        }
        else if (type == "COMMAND")
        {
            emit serverCommandReceived(payload);
        }
    }
}

void ClientChannel::onErrorOccurred(QAbstractSocket::SocketError)
{
    emit logMessage(m_socket.errorString());
}

} // namespace rmm::client::network
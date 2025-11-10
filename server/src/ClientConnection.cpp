#include "ClientConnection.h"

#include "ChatMessage.h"

#include <QHostAddress>
#include <QLoggingCategory>
#include <utility>

Q_LOGGING_CATEGORY(chatServer, "kukaracha.server")

ClientConnection::ClientConnection(QTcpSocket *socket, QObject *parent)
    : QObject(parent)
    , m_socket(socket)
{
    Q_ASSERT(m_socket);

    connect(m_socket, &QTcpSocket::readyRead, this, &ClientConnection::handleReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &ClientConnection::handleDisconnected);
}

void ClientConnection::sendMessage(const ChatMessage &message)
{
    const auto payload = m_serializer.serialize(message);
    auto framedPayload = payload;
    framedPayload.append('\n');
    const auto bytesWritten = m_socket->write(framedPayload);
    if (bytesWritten == -1) {
        qCWarning(chatServer) << "Failed to write to client" << m_socket->peerAddress() << m_socket->errorString();
    }
}

void ClientConnection::disconnectFromServer()
{
    if (!m_socket) {
        return;
    }

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

bool ClientConnection::hasUserName() const
{
    return !m_userName.isEmpty();
}

const QString &ClientConnection::userName() const
{
    return m_userName;
}

void ClientConnection::setUserName(QString userName)
{
    m_userName = std::move(userName);
}

bool ClientConnection::isAuthenticated() const
{
    return m_authenticated;
}

void ClientConnection::setAuthenticated(bool authenticated)
{
    m_authenticated = authenticated;
}

void ClientConnection::handleReadyRead()
{
    m_buffer.append(m_socket->readAll());

    int newlineIndex = -1;
    while ((newlineIndex = m_buffer.indexOf('\n')) != -1) {
        const auto payload = m_buffer.left(newlineIndex);
        m_buffer.remove(0, newlineIndex + 1);
        processPayload(payload);
    }
}

void ClientConnection::handleDisconnected()
{
    emit connectionClosed(this);
    m_socket->deleteLater();
    deleteLater();
}

void ClientConnection::processPayload(const QByteArray &payload)
{
    try {
        const auto message = m_serializer.deserialize(payload);
        emit messageReceived(message);
    } catch (const std::exception &error) {
        qCWarning(chatServer) << "Failed to parse message from client" << error.what();
    }
}


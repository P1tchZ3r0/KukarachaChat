#include "ChatClient.h"

#include <QDateTime>
#include <QLoggingCategory>

#include <exception>

Q_LOGGING_CATEGORY(chatClient, "kukaracha.client")

ChatClient::ChatClient(QObject *parent)
    : QObject(parent)
{
    connect(&m_socket, &QTcpSocket::readyRead, this, &ChatClient::handleReadyRead);
    connect(&m_socket, &QTcpSocket::connected, this, &ChatClient::handleConnected);
    connect(&m_socket, &QTcpSocket::disconnected, this, &ChatClient::handleDisconnected);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &ChatClient::handleSocketError);
}

void ChatClient::connectToServer(const QString &host, quint16 port, QString userName, QString password)
{
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.abort();
    }
    m_userName = std::move(userName);
    m_password = std::move(password);
    setAuthenticated(false);
    m_socket.connectToHost(host, port);
}

void ChatClient::disconnectFromServer()
{
    m_socket.disconnectFromHost();
}

void ChatClient::sendMessage(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return;
    }
    if (!isConnected()) {
        emit errorOccurred(tr("Нет подключения к серверу"));
        return;
    }

    if (!m_authenticated) {
        emit errorOccurred(tr("Сначала выполните вход"));
        return;
    }

    ChatMessage message{m_userName, text, QDateTime::currentDateTimeUtc()};
    auto payload = m_serializer.serialize(message);
    payload.append('\n');

    const auto bytesWritten = m_socket.write(payload);
    if (bytesWritten == -1) {
        emit errorOccurred(m_socket.errorString());
    }
}

bool ChatClient::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

const QString &ChatClient::userName() const
{
    return m_userName;
}

bool ChatClient::isAuthenticated() const
{
    return m_authenticated;
}

void ChatClient::handleReadyRead()
{
    m_buffer.append(m_socket.readAll());

    int newlineIndex = -1;
    while ((newlineIndex = m_buffer.indexOf('\n')) != -1) {
        const auto payload = m_buffer.left(newlineIndex);
        m_buffer.remove(0, newlineIndex + 1);
        processPayload(payload);
    }
}

void ChatClient::handleConnected()
{
    qCInfo(chatClient) << "Подключено к серверу";
    emit connectionStateChanged(true);
    sendAuthentication();
}

void ChatClient::handleDisconnected()
{
    qCInfo(chatClient) << "Отключено от сервера";
    emit connectionStateChanged(false);
    setAuthenticated(false);
}

void ChatClient::handleSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    emit errorOccurred(m_socket.errorString());
}

void ChatClient::processPayload(const QByteArray &payload)
{
    try {
        const auto message = m_serializer.deserialize(payload);

        if (message.sender() == QStringLiteral("SERVER")) {
            const auto text = message.text();
            if (text.startsWith(QStringLiteral("AUTH_OK"))) {
                setAuthenticated(true);
                emit messageReceived(ChatMessage{
                    QStringLiteral("SERVER"),
                    tr("Авторизация успешна"),
                    QDateTime::currentDateTimeUtc()
                });
                return;
            }

            if (text.startsWith(QStringLiteral("AUTH_FAIL:"))) {
                const auto reason = text.mid(QStringLiteral("AUTH_FAIL:").size()).trimmed();
                emit messageReceived(ChatMessage{
                    QStringLiteral("SERVER"),
                    tr("Авторизация не удалась: %1").arg(reason),
                    QDateTime::currentDateTimeUtc()
                });
                m_socket.disconnectFromHost();
                return;
            }
        }

        emit messageReceived(message);
    } catch (const std::exception &exception) {
        emit errorOccurred(tr("Некорректное сообщение от сервера: %1").arg(exception.what()));
    }
}

void ChatClient::setAuthenticated(bool authenticated)
{
    if (m_authenticated == authenticated) {
        return;
    }

    m_authenticated = authenticated;
    emit authenticatedChanged(m_authenticated);
}

void ChatClient::sendAuthentication()
{
    if (m_userName.trimmed().isEmpty()) {
        emit errorOccurred(tr("Логин не задан"));
        return;
    }

    ChatMessage authMessage{m_userName, m_password, QDateTime::currentDateTimeUtc()};
    auto payload = m_serializer.serialize(authMessage);
    payload.append('\n');
    const auto bytesWritten = m_socket.write(payload);
    if (bytesWritten == -1) {
        emit errorOccurred(m_socket.errorString());
    }
}


#include "ChatClient.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QStringList>

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
    // Если уже подключены, отключаемся
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_socket.abort();
    }
    
    // Сохраняем данные для авторизации
    m_userName = userName;
    m_password = password;
    setAuthenticated(false);
    
    // Подключаемся к серверу
    m_socket.connectToHost(host, port);
}

void ChatClient::disconnectFromServer()
{
    m_socket.disconnectFromHost();
}

void ChatClient::sendMessage(const QString &text)
{
    // Проверяем, что текст не пустой
    QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    
    // Проверяем подключение
    if (isConnected() == false) {
        emit errorOccurred(tr("Нет подключения к серверу"));
        return;
    }

    // Проверяем авторизацию
    if (m_authenticated == false) {
        emit errorOccurred(tr("Сначала выполните вход"));
        return;
    }

    // Создаем сообщение
    QDateTime currentTime = QDateTime::currentDateTimeUtc();
    ChatMessage message(m_userName, text, currentTime);
    
    // Сериализуем и отправляем
    QByteArray payload = m_serializer.serialize(message);
    payload.append('\n');

    qint64 bytesWritten = m_socket.write(payload);
    if (bytesWritten == -1) {
        QString error = m_socket.errorString();
        emit errorOccurred(error);
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
    // Читаем все доступные данные
    QByteArray data = m_socket.readAll();
    m_buffer.append(data);

    // Обрабатываем все сообщения в буфере
    int newlineIndex = m_buffer.indexOf('\n');
    while (newlineIndex != -1) {
        // Извлекаем одно сообщение
        QByteArray payload = m_buffer.left(newlineIndex);
        m_buffer.remove(0, newlineIndex + 1);
        
        // Обрабатываем сообщение
        processPayload(payload);
        
        // Ищем следующее сообщение
        newlineIndex = m_buffer.indexOf('\n');
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
        // Десериализуем сообщение
        ChatMessage message = m_serializer.deserialize(payload);

        // Проверяем, системное ли это сообщение
        QString sender = message.sender();
        if (sender == "SERVER") {
            QString text = message.text();
            
            // Обрабатываем успешную авторизацию
            if (text.startsWith("AUTH_OK")) {
                setAuthenticated(true);
                QDateTime currentTime = QDateTime::currentDateTimeUtc();
                ChatMessage successMsg("SERVER", tr("Авторизация успешна"), currentTime);
                emit messageReceived(successMsg);
                return;
            }

            // Обрабатываем ошибку авторизации
            if (text.startsWith("AUTH_FAIL:")) {
                int prefixLength = QString("AUTH_FAIL:").size();
                QString reason = text.mid(prefixLength);
                QString trimmedReason = reason.trimmed();
                QDateTime currentTime = QDateTime::currentDateTimeUtc();
                ChatMessage failMsg("SERVER", tr("Авторизация не удалась: %1").arg(trimmedReason), currentTime);
                emit messageReceived(failMsg);
                m_socket.disconnectFromHost();
                return;
            }

            // Обрабатываем список пользователей
            if (text.startsWith("USER_LIST:")) {
                int prefixLength = QString("USER_LIST:").size();
                QString userListStr = text.mid(prefixLength);
                QStringList userList;
                if (userListStr.isEmpty() == false) {
                    userList = userListStr.split(",", Qt::SkipEmptyParts);
                }
                emit userListReceived(userList);
                return;
            }
        }

        // Отправляем обычное сообщение
        emit messageReceived(message);
    } catch (const std::exception &exception) {
        QString errorMsg = exception.what();
        emit errorOccurred(tr("Некорректное сообщение от сервера: %1").arg(errorMsg));
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
    // Проверяем, что логин задан
    QString trimmedName = m_userName.trimmed();
    if (trimmedName.isEmpty()) {
        emit errorOccurred(tr("Логин не задан"));
        return;
    }

    // Создаем сообщение авторизации
    QDateTime currentTime = QDateTime::currentDateTimeUtc();
    ChatMessage authMessage(m_userName, m_password, currentTime);
    
    // Сериализуем и отправляем
    QByteArray payload = m_serializer.serialize(authMessage);
    payload.append('\n');
    
    qint64 bytesWritten = m_socket.write(payload);
    if (bytesWritten == -1) {
        QString error = m_socket.errorString();
        emit errorOccurred(error);
    }
}


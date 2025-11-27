#include "ChatServer.h"

#include "ChatMessage.h"
#include "ClientConnection.h"
#include "JsonMessageSerializer.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QTcpSocket>
#include <QtGlobal>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QDateTime>

#include <algorithm>
#include <optional>

Q_LOGGING_CATEGORY(chatServerCore, "kukaracha.server.core")

namespace {
bool parseAllowRegistration()
{
    int value = 0;
    if (qEnvironmentVariableIsSet("KUKARACHA_ALLOW_AUTO_REGISTER")) {
        value = qEnvironmentVariableIntValue("KUKARACHA_ALLOW_AUTO_REGISTER");
        return value != 0;
    }
    return false;
}

const QString kAdminUser = QStringLiteral("admin");
} // namespace

ChatServer::ChatServer(QObject *parent)
    : QTcpServer(parent)
    , m_userStore(QCoreApplication::applicationDirPath() + QStringLiteral("/users.json"))
    , m_allowRegistration(parseAllowRegistration())
{
    if (!m_userStore.load()) {
        qCWarning(chatServerCore) << "Не удалось загрузить базу пользователей, новые аккаунты не будут сохранены";
    }
    
    // Настройка пути для логов
    const auto appDir = QCoreApplication::applicationDirPath();
    const auto logsDir = appDir + QStringLiteral("/logs");
    QDir().mkpath(logsDir);
    
    const auto sessionStartTime = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_hh-mm-ss"));
    m_logFilePath = logsDir + QStringLiteral("/session_") + sessionStartTime + QStringLiteral(".log");
    
    qCInfo(chatServerCore) << "Логи сессии будут сохраняться в:" << m_logFilePath;
}

bool ChatServer::start(quint16 port)
{
    if (!listen(QHostAddress::Any, port)) {
        const auto errorMessage = tr("Не удалось запустить сервер: %1").arg(errorString());
        emit serverError(errorMessage);
        qCCritical(chatServerCore) << errorMessage;
        return false;
    }

    qCInfo(chatServerCore) << "Сервер запущен на порту" << serverPort();
    return true;
}

void ChatServer::stop()
{
    close();
    for (auto *client : m_clients) {
        client->deleteLater();
    }
    m_clients.clear();
    m_clientsByName.clear();
    qCInfo(chatServerCore) << "Сервер остановлен";
}

void ChatServer::incomingConnection(qintptr socketDescriptor)
{
    auto *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        qCWarning(chatServerCore) << "Не удалось принять подключение:" << socket->errorString();
        socket->deleteLater();
        return;
    }

    auto *connection = new ClientConnection(socket, this);
    connect(connection, &ClientConnection::messageReceived, this, [this, connection](const ChatMessage &message) {
        onMessageReceived(message, connection);
    });
    connect(connection, &ClientConnection::connectionClosed, this, &ChatServer::onConnectionClosed);

    m_clients.push_back(connection);

    qCInfo(chatServerCore) << "Новый клиент:" << socket->peerAddress().toString();
}

void ChatServer::onMessageReceived(const ChatMessage &message, ClientConnection *sender)
{
    if (!sender) {
        qCWarning(chatServerCore) << "Получено сообщение, но отправитель неизвестен";
        return;
    }

    const auto requestedName = message.sender().trimmed();
    if (!sender->isAuthenticated()) {
        if (requestedName.isEmpty()) {
            sender->sendMessage(ChatMessage{"SERVER", tr("AUTH_FAIL: Логин не может быть пустым")});
            sender->disconnectFromServer();
            return;
        }

        if (m_clientsByName.contains(requestedName)) {
            sender->sendMessage(ChatMessage{"SERVER", tr("AUTH_FAIL: Пользователь уже подключён")});
            sender->disconnectFromServer();
            return;
        }

        if (m_bannedUsers.contains(requestedName)) {
            sender->sendMessage(ChatMessage{"SERVER", tr("AUTH_FAIL: Пользователь заблокирован")});
            sender->disconnectFromServer();
            return;
        }

        QString errorMessage;
        UserStore::AuthResult authResult;
        const bool userExists = m_userStore.contains(requestedName);

        if (userExists) {
            authResult = m_userStore.authenticate(requestedName, message.text(), errorMessage);
        } else if (m_allowRegistration) {
            authResult = m_userStore.registerUser(requestedName, message.text(), errorMessage);
        } else {
            authResult = UserStore::AuthResult::UserNotFound;
            errorMessage = tr("Пользователь не найден. Обратитесь к администратору для регистрации");
        }

        switch (authResult) {
        case UserStore::AuthResult::SuccessExisting:
        case UserStore::AuthResult::RegisteredNew:
            sender->setUserName(requestedName);
            sender->setAuthenticated(true);
            m_clientsByName.insert(requestedName, sender);
            sender->sendMessage(ChatMessage{"SERVER", QStringLiteral("AUTH_OK")});
            if (authResult == UserStore::AuthResult::RegisteredNew) {
                sender->sendMessage(ChatMessage{"SERVER", tr("Создан новый аккаунт и выполнен вход")});
            } else {
                sender->sendMessage(ChatMessage{"SERVER", tr("Вход выполнен")});
            }
            qCInfo(chatServerCore) << "Пользователь авторизован:" << requestedName;
            
            // Отправляем историю сообщений новому пользователю
            sendMessageHistory(sender);
            
            // Отправляем список пользователей новому пользователю
            sendUserList(sender);
            
            broadcastSystemMessage(tr("%1 вошёл в чат").arg(requestedName));
            
            // Отправляем обновленный список пользователей всем (включая нового пользователя)
            broadcastUserList();
            break;
        case UserStore::AuthResult::WrongPassword:
        case UserStore::AuthResult::InvalidCredentials:
        case UserStore::AuthResult::StorageError:
        case UserStore::AuthResult::UserNotFound:
            sender->sendMessage(ChatMessage{
                QStringLiteral("SERVER"),
                QStringLiteral("AUTH_FAIL: %1").arg(errorMessage)
            });
            sender->disconnectFromServer();
            break;
        }
        return;
    }

    if (sender->userName() != requestedName) {
        sender->sendMessage(ChatMessage{"SERVER", tr("Нельзя менять имя пользователя во время сессии")});
        return;
    }

    const auto text = message.text().trimmed();
    if (text.isEmpty()) {
        return;
    }

    if (QString::compare(sender->userName(), kAdminUser, Qt::CaseInsensitive) == 0) {
        if (handleAdminCommand(ChatMessage{sender->userName(), text, message.timestamp()}, sender)) {
            return;
        }
    }

    qCInfo(chatServerCore) << "Сообщение от" << message.sender() << ':' << message.text();
    
    // Сохраняем сообщение в историю и лог
    const ChatMessage chatMessage{message.sender(), message.text(), message.timestamp()};
    addMessageToHistory(chatMessage);
    saveMessageToLog(chatMessage);
    
    // Отправляем всем клиентам
    for (auto *client : m_clients) {
        if (client) {
            client->sendMessage(chatMessage);
        }
    }
}

void ChatServer::onConnectionClosed(ClientConnection *connection)
{
    const auto end = std::remove(m_clients.begin(), m_clients.end(), connection);
    m_clients.erase(end, m_clients.end());
    if (connection && connection->hasUserName()) {
        const auto name = connection->userName();
        m_clientsByName.remove(name);
        broadcastSystemMessage(tr("%1 покинул чат").arg(name));
        // Отправляем обновленный список пользователей всем остальным
        broadcastUserList();
    }
    qCInfo(chatServerCore) << "Клиент отключился";
}

void ChatServer::broadcastSystemMessage(const QString &text)
{
    const ChatMessage systemMessage{QStringLiteral("SERVER"), text};
    addMessageToHistory(systemMessage);
    saveMessageToLog(systemMessage);
    
    for (auto *client : m_clients) {
        if (client) {
            client->sendMessage(systemMessage);
        }
    }
}

bool ChatServer::handleAdminCommand(const ChatMessage &message, ClientConnection *sender)
{
    if (!sender) {
        return false;
    }

    const auto text = message.text().trimmed();
    if (!text.startsWith(QLatin1Char('/'))) {
        return false;
    }

    const auto parts = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return false;
    }

    const auto command = parts.first().toLower();
    const auto requireTarget = [&](const QString &action) -> std::optional<QString> {
        if (parts.size() < 2) {
            sender->sendMessage(ChatMessage{
                QStringLiteral("SERVER"),
                tr("Команда %1 требует указать имя пользователя").arg(action)
            });
            return std::nullopt;
        }
        const auto target = parts.at(1).trimmed();
        if (target.isEmpty()) {
            sender->sendMessage(ChatMessage{
                QStringLiteral("SERVER"),
                tr("Имя пользователя не может быть пустым")
            });
            return std::nullopt;
        }
        if (QString::compare(target, sender->userName(), Qt::CaseInsensitive) == 0) {
            sender->sendMessage(ChatMessage{
                QStringLiteral("SERVER"),
                tr("Нельзя выполнить команду %1 на себе").arg(action)
            });
            return std::nullopt;
        }
        return target;
    };

    if (command == QStringLiteral("/kick")) {
        const auto targetOpt = requireTarget(QStringLiteral("/kick"));
        if (!targetOpt.has_value()) {
            return true;
        }
        const auto targetName = targetOpt.value();
        if (auto *target = findClientByName(targetName)) {
            target->sendMessage(ChatMessage{
                QStringLiteral("SERVER"),
                tr("Вас отключил администратор")
            });
            target->disconnectFromServer();
            sender->sendMessage(ChatMessage{
                QStringLiteral("SERVER"),
                tr("Пользователь %1 отключён").arg(targetName)
            });
        } else {
            sender->sendMessage(ChatMessage{
                QStringLiteral("SERVER"),
                tr("Пользователь %1 не найден").arg(targetName)
            });
        }
        return true;
    }

    if (command == QStringLiteral("/ban")) {
        const auto targetOpt = requireTarget(QStringLiteral("/ban"));
        if (!targetOpt.has_value()) {
            return true;
        }
        const auto targetName = targetOpt.value();
        if (m_bannedUsers.contains(targetName)) {
            sender->sendMessage(ChatMessage{
                QStringLiteral("SERVER"),
                tr("Пользователь %1 уже заблокирован").arg(targetName)
            });
            return true;
        }
        m_bannedUsers.insert(targetName);
        if (auto *target = findClientByName(targetName)) {
            target->sendMessage(ChatMessage{
                QStringLiteral("SERVER"),
                tr("Вы заблокированы администратором")
            });
            target->disconnectFromServer();
        }
        sender->sendMessage(ChatMessage{
            QStringLiteral("SERVER"),
            tr("Пользователь %1 заблокирован").arg(targetName)
        });
        return true;
    }

    if (command == QStringLiteral("/unban")) {
        const auto targetOpt = requireTarget(QStringLiteral("/unban"));
        if (!targetOpt.has_value()) {
            return true;
        }
        const auto targetName = targetOpt.value();
        if (!m_bannedUsers.contains(targetName)) {
            sender->sendMessage(ChatMessage{
                QStringLiteral("SERVER"),
                tr("Пользователь %1 не числится в бан-листе").arg(targetName)
            });
            return true;
        }
        m_bannedUsers.remove(targetName);
        sender->sendMessage(ChatMessage{
            QStringLiteral("SERVER"),
            tr("Пользователь %1 разблокирован").arg(targetName)
        });
        return true;
    }

    sender->sendMessage(ChatMessage{
        QStringLiteral("SERVER"),
        tr("Неизвестная команда: %1").arg(command)
    });
    return true;
}

ClientConnection *ChatServer::findClientByName(const QString &name) const
{
    const auto trimmed = name.trimmed();
    const auto it = m_clientsByName.find(trimmed);
    if (it != m_clientsByName.end()) {
        return it.value();
    }
    return nullptr;
}

void ChatServer::saveMessageToLog(const ChatMessage &message)
{
    QFile logFile(m_logFilePath);
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qCWarning(chatServerCore) << "Не удалось открыть файл лога для записи:" << m_logFilePath;
        return;
    }
    
    QTextStream stream(&logFile);
    
    const auto timestamp = message.timestamp().toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    stream << QStringLiteral("[%1] <%2> %3\n")
              .arg(timestamp, message.sender(), message.text());
    
    logFile.close();
}

void ChatServer::addMessageToHistory(const ChatMessage &message)
{
    m_messageHistory.push_back(message);
    
    // Ограничиваем размер истории
    if (m_messageHistory.size() > kMaxHistorySize) {
        m_messageHistory.pop_front();
    }
}

void ChatServer::sendMessageHistory(ClientConnection *client)
{
    if (!client || m_messageHistory.empty()) {
        return;
    }
    
    qCInfo(chatServerCore) << "Отправка истории из" << m_messageHistory.size() << "сообщений пользователю" << client->userName();
    
    // Отправляем системное сообщение о начале истории
    client->sendMessage(ChatMessage{
        QStringLiteral("SERVER"),
        tr("--- История сообщений (%1 сообщений) ---").arg(m_messageHistory.size())
    });
    
    // Отправляем все сообщения из истории
    for (const auto &message : m_messageHistory) {
        client->sendMessage(message);
    }
    
    // Отправляем системное сообщение о конце истории
    client->sendMessage(ChatMessage{
        QStringLiteral("SERVER"),
        tr("--- Конец истории ---")
    });
}

void ChatServer::sendUserList(ClientConnection *client)
{
    if (!client) {
        return;
    }
    
    QStringList userList;
    for (auto it = m_clientsByName.begin(); it != m_clientsByName.end(); ++it) {
        if (it.value() && it.value()->isAuthenticated()) {
            userList.append(it.key());
        }
    }
    
    const QString userListStr = QStringLiteral("USER_LIST:") + userList.join(QStringLiteral(","));
    client->sendMessage(ChatMessage{QStringLiteral("SERVER"), userListStr});
}

void ChatServer::broadcastUserList()
{
    QStringList userList;
    for (auto it = m_clientsByName.begin(); it != m_clientsByName.end(); ++it) {
        if (it.value() && it.value()->isAuthenticated()) {
            userList.append(it.key());
        }
    }
    
    const QString userListStr = QStringLiteral("USER_LIST:") + userList.join(QStringLiteral(","));
    const ChatMessage systemMessage{QStringLiteral("SERVER"), userListStr};
    
    for (auto *client : m_clients) {
        if (client && client->isAuthenticated()) {
            client->sendMessage(systemMessage);
        }
    }
}


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
    , m_userStore(QCoreApplication::applicationDirPath() + "/users.json")
    , m_allowRegistration(parseAllowRegistration())
{
    
    // Загружаем пользователей
    bool loaded = m_userStore.load();
    if (loaded == false) {
        qCWarning(chatServerCore) << "Не удалось загрузить базу пользователей, новые аккаунты не будут сохранены";
    }
    
    // Настройка пути для логов
    QString appDir = QCoreApplication::applicationDirPath();
    QString logsDir = appDir + "/logs";
    QDir dir;
    dir.mkpath(logsDir);
    
    // Создаем имя файла лога с текущей датой и временем
    QDateTime currentTime = QDateTime::currentDateTime();
    QString sessionStartTime = currentTime.toString("yyyy-MM-dd_hh-mm-ss");
    m_logFilePath = logsDir + "/session_" + sessionStartTime + ".log";
    
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
    // Закрываем сервер
    close();
    
    // Удаляем всех клиентов
    for (ClientConnection *client : m_clients) {
        if (client) {
            client->deleteLater();
        }
    }
    
    // Очищаем списки
    m_clients.clear();
    m_clientsByName.clear();
    
    qCInfo(chatServerCore) << "Сервер остановлен";
}

void ChatServer::incomingConnection(qintptr socketDescriptor)
{
    // Создаем новый сокет для клиента
    QTcpSocket *socket = new QTcpSocket(this);
    
    // Устанавливаем дескриптор сокета
    bool ok = socket->setSocketDescriptor(socketDescriptor);
    if (ok == false) {
        qCWarning(chatServerCore) << "Не удалось принять подключение:" << socket->errorString();
        socket->deleteLater();
        return;
    }

    // Создаем объект соединения с клиентом
    ClientConnection *connection = new ClientConnection(socket, this);
    
    // Подключаем сигналы
    connect(connection, &ClientConnection::messageReceived, this, [this, connection](const ChatMessage &message) {
        onMessageReceived(message, connection);
    });
    connect(connection, &ClientConnection::connectionClosed, this, &ChatServer::onConnectionClosed);

    // Добавляем в список клиентов
    m_clients.push_back(connection);

    qCInfo(chatServerCore) << "Новый клиент:" << socket->peerAddress().toString();
}

void ChatServer::onMessageReceived(const ChatMessage &message, ClientConnection *sender)
{
    // Проверяем, что отправитель существует
    if (sender == nullptr) {
        qCWarning(chatServerCore) << "Получено сообщение, но отправитель неизвестен";
        return;
    }

    // Получаем имя пользователя из сообщения
    QString messageSender = message.sender();
    QString requestedName = messageSender.trimmed();
    
    // Если пользователь еще не авторизован, обрабатываем авторизацию
    if (sender->isAuthenticated() == false) {
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
        
        // Проверяем, существует ли пользователь
        bool userExists = m_userStore.contains(requestedName);

        if (userExists) {
            // Пытаемся авторизовать существующего пользователя
            QString password = message.text();
            authResult = m_userStore.authenticate(requestedName, password, errorMessage);
        } else if (m_allowRegistration) {
            // Регистрируем нового пользователя
            QString password = message.text();
            authResult = m_userStore.registerUser(requestedName, password, errorMessage);
        } else {
            // Пользователь не найден и регистрация запрещена
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

    // Получаем текст сообщения
    QString messageText = message.text();
    QString trimmedText = messageText.trimmed();
    if (trimmedText.isEmpty()) {
        return;
    }

    // Проверяем, является ли отправитель администратором
    QString senderName = sender->userName();
    bool isAdmin = (QString::compare(senderName, kAdminUser, Qt::CaseInsensitive) == 0);
    if (isAdmin) {
        ChatMessage adminMessage(senderName, trimmedText, message.timestamp());
        bool handled = handleAdminCommand(adminMessage, sender);
        if (handled) {
            return;
        }
    }

    qCInfo(chatServerCore) << "Сообщение от" << message.sender() << ':' << message.text();
    
    // Сохраняем сообщение в историю и лог
    ChatMessage chatMessage(message.sender(), message.text(), message.timestamp());
    addMessageToHistory(chatMessage);
    saveMessageToLog(chatMessage);
    
    // Отправляем всем клиентам
    for (ClientConnection *client : m_clients) {
        if (client != nullptr) {
            client->sendMessage(chatMessage);
        }
    }
}

void ChatServer::onConnectionClosed(ClientConnection *connection)
{
    // Удаляем клиента из списка
    std::vector<ClientConnection *>::iterator it = m_clients.begin();
    while (it != m_clients.end()) {
        if (*it == connection) {
            it = m_clients.erase(it);
        } else {
            ++it;
        }
    }
    
    // Если у клиента было имя, удаляем его из списка имен
    if (connection != nullptr && connection->hasUserName()) {
        QString name = connection->userName();
        m_clientsByName.remove(name);
        QString message = tr("%1 покинул чат").arg(name);
        broadcastSystemMessage(message);
        
        // Отправляем обновленный список пользователей всем остальным
        broadcastUserList();
    }
    qCInfo(chatServerCore) << "Клиент отключился";
}

void ChatServer::broadcastSystemMessage(const QString &text)
{
    // Создаем системное сообщение
    ChatMessage systemMessage("SERVER", text);
    addMessageToHistory(systemMessage);
    saveMessageToLog(systemMessage);
    
    // Отправляем всем клиентам
    for (ClientConnection *client : m_clients) {
        if (client != nullptr) {
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
    // Открываем файл лога для записи
    QFile logFile(m_logFilePath);
    bool opened = logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    if (opened == false) {
        qCWarning(chatServerCore) << "Не удалось открыть файл лога для записи:" << m_logFilePath;
        return;
    }
    
    // Записываем сообщение в лог
    QTextStream stream(&logFile);
    
    QDateTime timestamp = message.timestamp();
    QDateTime localTime = timestamp.toLocalTime();
    QString timeString = localTime.toString("yyyy-MM-dd hh:mm:ss");
    QString sender = message.sender();
    QString text = message.text();
    
    stream << "[" << timeString << "] <" << sender << "> " << text << "\n";
    
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
    // Проверяем, что клиент существует и есть история
    if (client == nullptr || m_messageHistory.empty()) {
        return;
    }
    
    int historySize = m_messageHistory.size();
    qCInfo(chatServerCore) << "Отправка истории из" << historySize << "сообщений пользователю" << client->userName();
    
    // Отправляем системное сообщение о начале истории
    QString startMessage = tr("--- История сообщений (%1 сообщений) ---").arg(historySize);
    ChatMessage startMsg("SERVER", startMessage);
    client->sendMessage(startMsg);
    
    // Отправляем все сообщения из истории
    for (const ChatMessage &msg : m_messageHistory) {
        client->sendMessage(msg);
    }
    
    // Отправляем системное сообщение о конце истории
    QString endMessage = tr("--- Конец истории ---");
    ChatMessage endMsg("SERVER", endMessage);
    client->sendMessage(endMsg);
}

void ChatServer::sendUserList(ClientConnection *client)
{
    // Проверяем, что клиент существует
    if (client == nullptr) {
        return;
    }
    
    // Собираем список пользователей
    QStringList userList;
    QHash<QString, ClientConnection *>::iterator it = m_clientsByName.begin();
    while (it != m_clientsByName.end()) {
        ClientConnection *conn = it.value();
        if (conn != nullptr && conn->isAuthenticated()) {
            QString userName = it.key();
            userList.append(userName);
        }
        ++it;
    }
    
    // Формируем строку со списком пользователей
    QString userListStr = "USER_LIST:" + userList.join(",");
    ChatMessage msg("SERVER", userListStr);
    client->sendMessage(msg);
}

void ChatServer::broadcastUserList()
{
    // Собираем список всех авторизованных пользователей
    QStringList userList;
    for (auto it = m_clientsByName.begin(); it != m_clientsByName.end(); ++it) {
        ClientConnection *conn = it.value();
        if (conn != nullptr && conn->isAuthenticated()) {
            QString userName = it.key();
            userList.append(userName);
        }
    }
    
    // Формируем сообщение со списком пользователей
    QString userListStr = "USER_LIST:" + userList.join(",");
    ChatMessage systemMessage("SERVER", userListStr);
    
    // Отправляем всем авторизованным клиентам
    for (ClientConnection *client : m_clients) {
        if (client != nullptr && client->isAuthenticated()) {
            client->sendMessage(systemMessage);
        }
    }
}


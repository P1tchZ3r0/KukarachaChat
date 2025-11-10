#include "ChatServer.h"

#include "ChatMessage.h"
#include "ClientConnection.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QTcpSocket>
#include <QtGlobal>

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
            broadcastSystemMessage(tr("%1 вошёл в чат").arg(requestedName));
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
    for (auto *client : m_clients) {
        if (client) {
            client->sendMessage(ChatMessage{message.sender(), message.text(), message.timestamp()});
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
    }
    qCInfo(chatServerCore) << "Клиент отключился";
}

void ChatServer::broadcastSystemMessage(const QString &text)
{
    for (auto *client : m_clients) {
        if (client) {
            client->sendMessage(ChatMessage{QStringLiteral("SERVER"), text});
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


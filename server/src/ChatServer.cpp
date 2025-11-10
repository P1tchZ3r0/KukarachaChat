#include "ChatServer.h"

#include "ChatMessage.h"
#include "ClientConnection.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QTcpSocket>
#include <QtGlobal>

#include <algorithm>

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
            for (auto *client : m_clients) {
                if (client && client != sender) {
                    client->sendMessage(ChatMessage{
                        QStringLiteral("SERVER"),
                        tr("%1 присоединился к чату").arg(requestedName)
                    });
                }
            }
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
        m_clientsByName.remove(connection->userName());
    }
    qCInfo(chatServerCore) << "Клиент отключился";
}


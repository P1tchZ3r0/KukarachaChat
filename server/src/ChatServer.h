#pragma once

#include "UserStore.h"

#include <QTcpServer>
#include <QHash>
#include <QSet>
#include <QString>
#include <vector>

class ClientConnection;
class ChatMessage;

class ChatServer final : public QTcpServer {
  Q_OBJECT

public:
  explicit ChatServer(QObject *parent = nullptr);

  bool start(quint16 port);
  void stop();

signals:
  void serverError(const QString &message);

protected:
  void incomingConnection(qintptr socketDescriptor) override;

private:
  void onMessageReceived(const ChatMessage &message, ClientConnection *sender);
  void onConnectionClosed(ClientConnection *connection);
  void broadcastSystemMessage(const QString &text);
  bool handleAdminCommand(const ChatMessage &message, ClientConnection *sender);
  ClientConnection *findClientByName(const QString &name) const;

  std::vector<ClientConnection *> m_clients;
  QHash<QString, ClientConnection *> m_clientsByName;
  UserStore m_userStore;
  bool m_allowRegistration = false;
  QSet<QString> m_bannedUsers;
};

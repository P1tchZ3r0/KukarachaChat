#pragma once

#include "UserStore.h"
#include "ChatMessage.h"

#include <QTcpServer>
#include <QHash>
#include <QSet>
#include <QString>
#include <vector>
#include <deque>

class ClientConnection;

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
  void saveMessageToLog(const ChatMessage &message);
  void sendMessageHistory(ClientConnection *client);
  void addMessageToHistory(const ChatMessage &message);
  void sendUserList(ClientConnection *client);
  void broadcastUserList();

  std::vector<ClientConnection *> m_clients;
  QHash<QString, ClientConnection *> m_clientsByName;
  UserStore m_userStore;
  bool m_allowRegistration = false;
  QSet<QString> m_bannedUsers;
  std::deque<ChatMessage> m_messageHistory;
  static constexpr size_t kMaxHistorySize = 1000;
  QString m_logFilePath;
};

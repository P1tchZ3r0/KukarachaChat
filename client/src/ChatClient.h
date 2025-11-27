#pragma once

#include "ChatMessage.h"
#include "JsonMessageSerializer.h"

#include <QObject>
#include <QTcpSocket>

class ChatClient final : public QObject {
    Q_OBJECT

public:
    explicit ChatClient(QObject *parent = nullptr);

    void connectToServer(const QString &host, quint16 port, QString userName, QString password);
    void disconnectFromServer();
    void sendMessage(const QString &text);

    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] const QString &userName() const;
    [[nodiscard]] bool isAuthenticated() const;

signals:
    void messageReceived(const ChatMessage &message);
    void connectionStateChanged(bool connected);
    void errorOccurred(const QString &message);
    void authenticatedChanged(bool authenticated);
    void userListReceived(const QStringList &users);

private slots:
    void handleReadyRead();
    void handleConnected();
    void handleDisconnected();
    void handleSocketError(QAbstractSocket::SocketError error);

private:
    void processPayload(const QByteArray &payload);
    void setAuthenticated(bool authenticated);
    void sendAuthentication();

    QTcpSocket m_socket;
    JsonMessageSerializer m_serializer;
    QByteArray m_buffer;
    QString m_userName;
    QString m_password;
    bool m_authenticated = false;
};


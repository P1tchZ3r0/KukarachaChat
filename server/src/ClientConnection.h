#pragma once

#include "ChatMessage.h"
#include "JsonMessageSerializer.h"

#include <QObject>
#include <QTcpSocket>
#include <QString>
#include <memory>

class ChatMessage;

class ClientConnection final : public QObject {
    Q_OBJECT

public:
    explicit ClientConnection(QTcpSocket *socket, QObject *parent = nullptr);

    void sendMessage(const ChatMessage &message);
    void disconnectFromServer();

    [[nodiscard]] bool hasUserName() const;
    [[nodiscard]] const QString &userName() const;
    void setUserName(QString userName);
    [[nodiscard]] bool isAuthenticated() const;
    void setAuthenticated(bool authenticated);

signals:
    void messageReceived(const ChatMessage &message);
    void connectionClosed(ClientConnection *connection);

private slots:
    void handleReadyRead();
    void handleDisconnected();

private:
    void processPayload(const QByteArray &payload);

    QTcpSocket *m_socket;
    JsonMessageSerializer m_serializer;
    QByteArray m_buffer;
    QString m_userName;
    bool m_authenticated = false;
};


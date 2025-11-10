#pragma once

#include <QByteArray>

class ChatMessage;

class IMessageSerializer {
public:
    virtual ~IMessageSerializer() = default;

    [[nodiscard]] virtual QByteArray serialize(const ChatMessage &message) const = 0;
    [[nodiscard]] virtual ChatMessage deserialize(const QByteArray &payload) const = 0;
};


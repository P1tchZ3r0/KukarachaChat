#include "JsonMessageSerializer.h"

#include "ChatMessage.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <stdexcept>

namespace {
constexpr auto kSenderKey = "sender";
constexpr auto kTextKey = "text";
constexpr auto kTimestampKey = "timestamp";
}

QByteArray JsonMessageSerializer::serialize(const ChatMessage &message) const
{
    QJsonObject object{
        {QString::fromLatin1(kSenderKey), message.sender()},
        {QString::fromLatin1(kTextKey), message.text()},
        {QString::fromLatin1(kTimestampKey), message.timestamp().toString(Qt::ISODateWithMs)}
    };

    QJsonDocument document{object};
    return document.toJson(QJsonDocument::Compact);
}

ChatMessage JsonMessageSerializer::deserialize(const QByteArray &payload) const
{
    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        throw std::runtime_error("Invalid message payload: not a JSON object");
    }

    const auto object = document.object();
    const auto sender = object.value(QString::fromLatin1(kSenderKey)).toString();
    const auto text = object.value(QString::fromLatin1(kTextKey)).toString();
    const auto timestampString = object.value(QString::fromLatin1(kTimestampKey)).toString();
    const auto timestamp = QDateTime::fromString(timestampString, Qt::ISODateWithMs);

    if (sender.isEmpty() || text.isEmpty() || !timestamp.isValid()) {
        throw std::runtime_error("Invalid message payload: missing required fields");
    }

    return ChatMessage{sender, text, timestamp.toUTC()};
}


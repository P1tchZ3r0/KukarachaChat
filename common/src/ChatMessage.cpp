#include "ChatMessage.h"

#include <utility>

ChatMessage::ChatMessage(QString sender, QString text, QDateTime timestamp)
    : m_sender(std::move(sender)), m_text(std::move(text)), m_timestamp(std::move(timestamp))
{
}

const QString &ChatMessage::sender() const
{
    return m_sender;
}

const QString &ChatMessage::text() const
{
    return m_text;
}

const QDateTime &ChatMessage::timestamp() const
{
    return m_timestamp;
}

void ChatMessage::setSender(const QString &sender)
{
    m_sender = sender;
}

void ChatMessage::setText(const QString &text)
{
    m_text = text;
}

void ChatMessage::setTimestamp(const QDateTime &timestamp)
{
    m_timestamp = timestamp;
}


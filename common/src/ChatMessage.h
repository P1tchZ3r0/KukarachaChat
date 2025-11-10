#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

class ChatMessage {
public:
    ChatMessage() = default;
    ChatMessage(QString sender, QString text, QDateTime timestamp = QDateTime::currentDateTimeUtc());

    [[nodiscard]] const QString &sender() const;
    [[nodiscard]] const QString &text() const;
    [[nodiscard]] const QDateTime &timestamp() const;

    void setSender(const QString &sender);
    void setText(const QString &text);
    void setTimestamp(const QDateTime &timestamp);

private:
    QString m_sender;
    QString m_text;
    QDateTime m_timestamp;
};

Q_DECLARE_METATYPE(ChatMessage)


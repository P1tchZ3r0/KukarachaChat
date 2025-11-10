#pragma once

#include "IMessageSerializer.h"

class JsonMessageSerializer final : public IMessageSerializer {
public:
    [[nodiscard]] QByteArray serialize(const ChatMessage &message) const override;
    [[nodiscard]] ChatMessage deserialize(const QByteArray &payload) const override;
};


#pragma once

#include <QHash>
#include <QString>

class UserStore {
public:
    enum class AuthResult {
        SuccessExisting,
        RegisteredNew,
        WrongPassword,
        InvalidCredentials,
        StorageError,
        UserNotFound
    };

    explicit UserStore(QString storagePath);

    [[nodiscard]] bool load();
    [[nodiscard]] bool isLoaded() const;
    [[nodiscard]] bool contains(const QString &login) const;

    [[nodiscard]] AuthResult authenticate(const QString &login, const QString &password, QString &errorMessage);
    [[nodiscard]] AuthResult registerUser(const QString &login, const QString &password, QString &errorMessage);

private:
    struct UserRecord {
        QString salt;
        QString passwordHash;
    };

    [[nodiscard]] QString hashPassword(const QString &salt, const QString &password) const;
    [[nodiscard]] bool save() const;

    bool m_loaded = false;
    QString m_storagePath;
    QHash<QString, UserRecord> m_users;
};



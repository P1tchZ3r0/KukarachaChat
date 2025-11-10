#include "UserStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QCryptographicHash>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <utility>

namespace {
Q_LOGGING_CATEGORY(chatUserStore, "kukaracha.server.auth")

constexpr auto kUsersKey = "users";
constexpr auto kLoginKey = "login";
constexpr auto kSaltKey = "salt";
constexpr auto kHashKey = "hash";

QString randomSalt()
{
    auto generator = QRandomGenerator::system();
    const auto value = generator->generate64();
    return QString::number(static_cast<qulonglong>(value), 16);
}
} // namespace

UserStore::UserStore(QString storagePath)
    : m_storagePath(std::move(storagePath))
{
}

bool UserStore::load()
{
    QFile file(m_storagePath);
    if (!file.exists()) {
        const QFileInfo info(file);
        QDir().mkpath(info.path());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qCWarning(chatUserStore) << "Не удалось создать файл пользователей:" << file.errorString();
            return false;
        }
        const QJsonDocument doc(QJsonObject{{QString::fromLatin1(kUsersKey), QJsonArray()}});
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(chatUserStore) << "Не удалось открыть файл пользователей:" << file.errorString();
        return false;
    }

    const auto data = file.readAll();
    file.close();

    const auto doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        qCWarning(chatUserStore) << "Файл пользователей повреждён";
        return false;
    }

    const auto usersArray = doc.object().value(QString::fromLatin1(kUsersKey)).toArray();
    m_users.clear();
    for (const auto &value : usersArray) {
        const auto userObject = value.toObject();
        const auto login = userObject.value(QString::fromLatin1(kLoginKey)).toString();
        const auto salt = userObject.value(QString::fromLatin1(kSaltKey)).toString();
        const auto hash = userObject.value(QString::fromLatin1(kHashKey)).toString();
        if (login.isEmpty() || salt.isEmpty() || hash.isEmpty()) {
            qCWarning(chatUserStore) << "Пропущена запись пользователя из-за некорректных данных";
            continue;
        }
        m_users.insert(login, UserRecord{salt, hash});
    }

    m_loaded = true;
    return true;
}

bool UserStore::isLoaded() const
{
    return m_loaded;
}

bool UserStore::contains(const QString &login) const
{
    return m_users.contains(login);
}

UserStore::AuthResult UserStore::authenticate(const QString &login, const QString &password, QString &errorMessage)
{
    if (!m_loaded) {
        if (!load()) {
            errorMessage = QObject::tr("Не удалось открыть базу пользователей");
            return AuthResult::StorageError;
        }
    }

    const auto trimmedLogin = login.trimmed();
    if (trimmedLogin.isEmpty()) {
        errorMessage = QObject::tr("Логин не может быть пустым");
        return AuthResult::InvalidCredentials;
    }

    if (password.isEmpty()) {
        errorMessage = QObject::tr("Пароль не может быть пустым");
        return AuthResult::InvalidCredentials;
    }

    if (m_users.contains(trimmedLogin)) {
        errorMessage = QObject::tr("Пользователь уже существует");
        return AuthResult::SuccessExisting;
    }

    auto it = m_users.find(trimmedLogin);
    if (it == m_users.end()) {
        errorMessage = QObject::tr("Пользователь не найден");
        return AuthResult::UserNotFound;
    }

    const auto expectedHash = it->passwordHash;
    const auto salt = it->salt;
    const auto actualHash = hashPassword(salt, password);
    if (QString::compare(expectedHash, actualHash, Qt::CaseSensitive) != 0) {
        errorMessage = QObject::tr("Неверный пароль");
        return AuthResult::WrongPassword;
    }

    return AuthResult::SuccessExisting;
}

UserStore::AuthResult UserStore::registerUser(const QString &login, const QString &password, QString &errorMessage)
{
    if (!m_loaded && !load()) {
        errorMessage = QObject::tr("Не удалось открыть базу пользователей");
        return AuthResult::StorageError;
    }

    const auto trimmedLogin = login.trimmed();
    if (trimmedLogin.isEmpty()) {
        errorMessage = QObject::tr("Логин не может быть пустым");
        return AuthResult::InvalidCredentials;
    }

    if (password.isEmpty()) {
        errorMessage = QObject::tr("Пароль не может быть пустым");
        return AuthResult::InvalidCredentials;
    }

    const auto salt = randomSalt();
    const auto hash = hashPassword(salt, password);
    m_users.insert(trimmedLogin, UserRecord{salt, hash});
    if (!save()) {
        errorMessage = QObject::tr("Не удалось сохранить нового пользователя");
        m_users.remove(trimmedLogin);
        return AuthResult::StorageError;
    }
    qCInfo(chatUserStore) << "Создан новый пользователь" << trimmedLogin;
    return AuthResult::RegisteredNew;
}

QString UserStore::hashPassword(const QString &salt, const QString &password) const
{
    QByteArray data = salt.toUtf8();
    data.append("::");
    data.append(password.toUtf8());
    const auto digest = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex());
}

bool UserStore::save() const
{
    QFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(chatUserStore) << "Не удалось записать файл пользователей:" << file.errorString();
        return false;
    }

    QJsonArray usersArray;
    for (auto it = m_users.constBegin(); it != m_users.constEnd(); ++it) {
        QJsonObject object{
            {QString::fromLatin1(kLoginKey), it.key()},
            {QString::fromLatin1(kSaltKey), it.value().salt},
            {QString::fromLatin1(kHashKey), it.value().passwordHash}
        };
        usersArray.push_back(object);
    }

    const QJsonDocument doc(QJsonObject{{QString::fromLatin1(kUsersKey), usersArray}});
    const auto bytesWritten = file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    if (bytesWritten == -1) {
        qCWarning(chatUserStore) << "Не удалось записать файл пользователей";
        return false;
    }

    return true;
}



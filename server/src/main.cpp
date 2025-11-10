#include "ChatServer.h"
#include "ChatMessage.h"

#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("KukarachaServer"));
    application.setOrganizationName(QStringLiteral("Kukaracha"));

    qRegisterMetaType<ChatMessage>();

    ChatServer server;
    QObject::connect(&server, &ChatServer::serverError, [](const QString &error) {
        qCritical() << error;
    });

    const auto args = application.arguments();
    quint16 port = 4242;
    if (args.size() > 1) {
        bool ok = false;
        const auto parsedPort = args.at(1).toUShort(&ok);
        if (ok) {
            port = parsedPort;
        }
    }

    if (!server.start(port)) {
        return EXIT_FAILURE;
    }

    const auto exitCode = QCoreApplication::exec();
    server.stop();
    return exitCode;
}


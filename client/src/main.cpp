#include "ChatMessage.h"
#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("KukarachaClient"));
    application.setOrganizationName(QStringLiteral("Kukaracha"));

    qRegisterMetaType<ChatMessage>();

    MainWindow window;
    window.show();

    return QApplication::exec();
}


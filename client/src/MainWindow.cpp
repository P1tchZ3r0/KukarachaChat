#include "MainWindow.h"

#include "ChatClient.h"
#include "ChatMessage.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QTextEdit>
#include <QToolBar>
#include <QVBoxLayout>
#include <QIcon>

#include <memory>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_client(std::make_unique<ChatClient>(this))
{
    buildUi();
    bindSignals();
    appendSystemMessage(tr("Введите данные сервера и нажмите Подключиться"));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    auto *connectionLayout = new QHBoxLayout();
    connectionLayout->addWidget(new QLabel(tr("Хост:"), central));
    m_hostEdit = new QLineEdit(QStringLiteral("127.0.0.1"), central);
    connectionLayout->addWidget(m_hostEdit);

    connectionLayout->addWidget(new QLabel(tr("Порт:"), central));
    m_portEdit = new QLineEdit(QString::number(4242), central);
    m_portEdit->setMaximumWidth(80);
    connectionLayout->addWidget(m_portEdit);

    connectionLayout->addWidget(new QLabel(tr("Имя:"), central));
    m_userNameEdit = new QLineEdit(QStringLiteral("User"), central);
    connectionLayout->addWidget(m_userNameEdit);

    connectionLayout->addWidget(new QLabel(tr("Пароль:"), central));
    m_passwordEdit = new QLineEdit(central);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    connectionLayout->addWidget(m_passwordEdit);

    m_connectButton = new QPushButton(tr("Подключиться"), central);
    connectionLayout->addWidget(m_connectButton);
    connectionLayout->addStretch();

    layout->addLayout(connectionLayout);

    m_chatView = new QTextEdit(central);
    m_chatView->setReadOnly(true);
    layout->addWidget(m_chatView, 1);

    auto *messageLayout = new QHBoxLayout();
    m_messageEdit = new QLineEdit(central);
    m_messageEdit->setPlaceholderText(tr("Введите сообщение..."));
    messageLayout->addWidget(m_messageEdit, 1);

    m_sendButton = new QPushButton(tr("Отправить"), central);
    messageLayout->addWidget(m_sendButton);

    layout->addLayout(messageLayout);

    setCentralWidget(central);
    const auto windowIcon = QIcon::fromTheme(QStringLiteral("mail-message-new"), QIcon::fromTheme(QStringLiteral("dialog-information")));
    if (!windowIcon.isNull()) {
        setWindowIcon(windowIcon);
    }
    setWindowTitle(tr("Кукарача Мессенджер"));

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayIcon = new QSystemTrayIcon(this);
        if (!windowIcon.isNull()) {
            m_trayIcon->setIcon(windowIcon);
        }
        m_trayIcon->setVisible(true);
    }

    resize(600, 400);
    updateControls();
}

void MainWindow::bindSignals()
{
    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(m_messageEdit, &QLineEdit::returnPressed, this, &MainWindow::onSendClicked);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);

    connect(m_client.get(), &ChatClient::messageReceived, this, &MainWindow::onMessageReceived);
    connect(m_client.get(), &ChatClient::connectionStateChanged, this, &MainWindow::onConnectionStateChanged);
    connect(m_client.get(), &ChatClient::errorOccurred, this, &MainWindow::onErrorOccurred);
    connect(m_client.get(), &ChatClient::authenticatedChanged, this, &MainWindow::onAuthenticatedChanged);
}

void MainWindow::onSendClicked()
{
    if (!m_client->isConnected() || !m_authenticated) {
        appendSystemMessage(tr("Сначала подключитесь к серверу"));
        return;
    }

    const auto text = m_messageEdit->text();
    if (text.trimmed().isEmpty()) {
        return;
    }

    m_client->sendMessage(text);
    m_messageEdit->clear();
}

void MainWindow::onConnectClicked()
{
    if (m_client->isConnected()) {
        m_client->disconnectFromServer();
        return;
    }

    bool ok = false;
    const auto port = m_portEdit->text().toUShort(&ok);
    if (!ok) {
        onErrorOccurred(tr("Некорректный порт"));
        return;
    }

    const auto host = m_hostEdit->text();
    const auto name = m_userNameEdit->text().trimmed();
    if (name.isEmpty()) {
        onErrorOccurred(tr("Введите имя пользователя"));
        return;
    }

    const auto password = m_passwordEdit->text();
    if (password.isEmpty()) {
        onErrorOccurred(tr("Введите пароль"));
        return;
    }

    m_client->connectToServer(host, port, name, password);
}

void MainWindow::onMessageReceived(const ChatMessage &message)
{
    if (message.sender() == QStringLiteral("SERVER")) {
        appendSystemMessage(message.text());
        return;
    }

    const auto timeStamp = message.timestamp().toLocalTime().toString("hh:mm:ss");
    const auto escapedSender = htmlEscape(message.sender());
    const auto escapedText = htmlEscape(message.text());
    const auto escapedTime = htmlEscape(timeStamp);
    const bool isSelf = QString::compare(message.sender(), m_client->userName(), Qt::CaseInsensitive) == 0;
    const auto senderStyle = isSelf ? QStringLiteral("font-weight:600;color:#5fb8ff;") : QStringLiteral("font-weight:600;color:#ffffff;");
    const auto textStyle = isSelf ? QStringLiteral("color:#d0ecff;") : QStringLiteral("color:#ffffff;");
    const auto html = QStringLiteral("<div><span style=\"color:#888888\">[%1]</span> <span style=\"%2\">%3</span>: <span style=\"%4\">%5</span></div>")
                          .arg(escapedTime, senderStyle, escapedSender, textStyle, escapedText);
    m_chatView->append(html);
    showMessageNotification(message);
}

void MainWindow::onConnectionStateChanged(bool connected)
{
    if (!connected) {
        m_authenticated = false;
    }
    updateControls();
    appendSystemMessage(connected ? tr("Подключение установлено") : tr("Подключение закрыто"));
}

void MainWindow::onErrorOccurred(const QString &message)
{
    appendSystemMessage(tr("Ошибка: %1").arg(message));
}

void MainWindow::appendSystemMessage(const QString &message)
{
    const auto timeStamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    const auto escapedTime = htmlEscape(timeStamp);
    const auto escapedText = htmlEscape(message);
    const auto html = QStringLiteral("<div style=\"color:#7f8c99\">[%1] <i>%2</i></div>").arg(escapedTime, escapedText);
    m_chatView->append(html);
}

void MainWindow::onAuthenticatedChanged(bool authenticated)
{
    m_authenticated = authenticated;
    updateControls();
    if (authenticated) {
        appendSystemMessage(tr("Вы успешно вошли в систему"));
    }
}

QString MainWindow::htmlEscape(const QString &text)
{
    QString escaped = text.toHtmlEscaped();
    return escaped.replace(QStringLiteral("\n"), QStringLiteral("<br/>"));
}

void MainWindow::updateControls()
{
    const bool connected = m_client->isConnected();
    m_sendButton->setEnabled(connected && m_authenticated);
    m_messageEdit->setEnabled(connected && m_authenticated);
    m_connectButton->setText(connected ? tr("Отключиться") : tr("Подключиться"));
}

void MainWindow::showMessageNotification(const ChatMessage &message)
{
    if (message.sender() == m_client->userName()) {
        return;
    }

    if (isActiveWindow()) {
        return;
    }

    QApplication::alert(this);
    if (m_trayIcon && QSystemTrayIcon::isSystemTrayAvailable()) {
        const auto title = tr("Новое сообщение");
        const auto body = QStringLiteral("%1: %2").arg(message.sender(), message.text());
        m_trayIcon->showMessage(title, body, QSystemTrayIcon::Information, 4000);
    }
}


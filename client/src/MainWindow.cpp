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
#include <QSettings>
#include <QPalette>
#include <QListWidget>
#include <QSplitter>

#include <memory>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_client(std::make_unique<ChatClient>(this))
{
    loadTheme();
    buildUi();
    bindSignals();
    applyTheme(m_currentTheme);
    appendSystemMessage(tr("Введите данные сервера и нажмите Подключиться"));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    m_centralWidget = new QWidget(this);
    auto *mainLayout = new QHBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Основная часть (чат)
    auto *chatWidget = new QWidget(m_centralWidget);
    auto *layout = new QVBoxLayout(chatWidget);

    auto *connectionLayout = new QHBoxLayout();
    connectionLayout->addWidget(new QLabel(tr("Хост:"), chatWidget));
    m_hostEdit = new QLineEdit(QStringLiteral("127.0.0.1"), chatWidget);
    connectionLayout->addWidget(m_hostEdit);

    connectionLayout->addWidget(new QLabel(tr("Порт:"), chatWidget));
    m_portEdit = new QLineEdit(QString::number(4242), chatWidget);
    m_portEdit->setMaximumWidth(80);
    connectionLayout->addWidget(m_portEdit);

    connectionLayout->addWidget(new QLabel(tr("Имя:"), chatWidget));
    m_userNameEdit = new QLineEdit(QStringLiteral("User"), chatWidget);
    connectionLayout->addWidget(m_userNameEdit);

    connectionLayout->addWidget(new QLabel(tr("Пароль:"), chatWidget));
    m_passwordEdit = new QLineEdit(chatWidget);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    connectionLayout->addWidget(m_passwordEdit);

    m_connectButton = new QPushButton(tr("Подключиться"), chatWidget);
    connectionLayout->addWidget(m_connectButton);
    
    m_themeButton = new QPushButton(tr("Тема"), chatWidget);
    m_themeButton->setMaximumWidth(80);
    connectionLayout->addWidget(m_themeButton);
    
    connectionLayout->addStretch();

    layout->addLayout(connectionLayout);

    m_chatView = new QTextEdit(chatWidget);
    m_chatView->setReadOnly(true);
    layout->addWidget(m_chatView, 1);

    auto *messageLayout = new QHBoxLayout();
    m_messageEdit = new QLineEdit(chatWidget);
    m_messageEdit->setPlaceholderText(tr("Введите сообщение..."));
    messageLayout->addWidget(m_messageEdit, 1);

    m_sendButton = new QPushButton(tr("Отправить"), chatWidget);
    messageLayout->addWidget(m_sendButton);

    layout->addLayout(messageLayout);

    // Панель со списком пользователей
    m_userListPanel = new QWidget(m_centralWidget);
    auto *userListLayout = new QVBoxLayout(m_userListPanel);
    userListLayout->setContentsMargins(5, 5, 5, 5);
    
    m_userListLabel = new QLabel(tr("Пользователи:"), m_userListPanel);
    userListLayout->addWidget(m_userListLabel);
    
    m_userListWidget = new QListWidget(m_userListPanel);
    m_userListWidget->setMaximumWidth(200);
    m_userListWidget->setMinimumWidth(150);
    userListLayout->addWidget(m_userListWidget);

    // Размещаем основную часть и панель пользователей
    mainLayout->addWidget(chatWidget, 1);
    mainLayout->addWidget(m_userListPanel, 0);

    setCentralWidget(m_centralWidget);
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
    connect(m_themeButton, &QPushButton::clicked, this, &MainWindow::onThemeChanged);

    connect(m_client.get(), &ChatClient::messageReceived, this, &MainWindow::onMessageReceived);
    connect(m_client.get(), &ChatClient::connectionStateChanged, this, &MainWindow::onConnectionStateChanged);
    connect(m_client.get(), &ChatClient::errorOccurred, this, &MainWindow::onErrorOccurred);
    connect(m_client.get(), &ChatClient::authenticatedChanged, this, &MainWindow::onAuthenticatedChanged);
    connect(m_client.get(), &ChatClient::userListReceived, this, &MainWindow::updateUserList);
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
        ChatEntry entry;
        entry.sender = QStringLiteral("SERVER");
        entry.text = message.text();
        entry.timestamp = message.timestamp();
        entry.isSystem = true;
        m_chatHistory.append(entry);
        
        const auto timeStamp = message.timestamp().toLocalTime().toString("hh:mm:ss");
        const auto escapedTime = htmlEscape(timeStamp);
        const auto escapedText = htmlEscape(message.text());
        const QString systemColor = m_currentTheme == Theme::Dark ? QStringLiteral("#7f8c99") : QStringLiteral("#666666");
        const auto html = QStringLiteral("<div style=\"color:%1\">[%2] <i>%3</i></div>").arg(systemColor, escapedTime, escapedText);
        m_chatView->append(html);
        return;
    }

    ChatEntry entry;
    entry.sender = message.sender();
    entry.text = message.text();
    entry.timestamp = message.timestamp();
    entry.isSystem = false;
    m_chatHistory.append(entry);

    const auto timeStamp = message.timestamp().toLocalTime().toString("hh:mm:ss");
    const auto escapedSender = htmlEscape(message.sender());
    const auto escapedText = htmlEscape(message.text());
    const auto escapedTime = htmlEscape(timeStamp);
    const bool isSelf = QString::compare(message.sender(), m_client->userName(), Qt::CaseInsensitive) == 0;
    
    QString senderStyle, textStyle, timeStyle;
    if (m_currentTheme == Theme::Dark) {
        timeStyle = QStringLiteral("color:#888888;");
        senderStyle = isSelf ? QStringLiteral("font-weight:600;color:#5fb8ff;") : QStringLiteral("font-weight:600;color:#ffffff;");
        textStyle = isSelf ? QStringLiteral("color:#d0ecff;") : QStringLiteral("color:#ffffff;");
    } else {
        timeStyle = QStringLiteral("color:#666666;");
        senderStyle = isSelf ? QStringLiteral("font-weight:600;color:#0066cc;") : QStringLiteral("font-weight:600;color:#000000;");
        textStyle = isSelf ? QStringLiteral("color:#003366;") : QStringLiteral("color:#000000;");
    }
    
    const auto html = QStringLiteral("<div><span style=\"%1\">[%2]</span> <span style=\"%3\">%4</span>: <span style=\"%5\">%6</span></div>")
                          .arg(timeStyle, escapedTime, senderStyle, escapedSender, textStyle, escapedText);
    m_chatView->append(html);
    showMessageNotification(message);
}

void MainWindow::onConnectionStateChanged(bool connected)
{
    if (!connected) {
        m_authenticated = false;
        // Очищаем историю при отключении, чтобы избежать дубликатов при повторном подключении
        m_chatHistory.clear();
        m_chatView->clear();
        m_userListWidget->clear();
    }
    updateControls();
    appendSystemMessage(connected ? tr("Подключение установлено") : tr("Подключение закрыто"));
}

void MainWindow::updateUserList(const QStringList &users)
{
    m_userListWidget->clear();
    for (const auto &user : users) {
        m_userListWidget->addItem(user);
    }
    m_userListLabel->setText(tr("Пользователи (%1):").arg(users.size()));
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
    const QString systemColor = m_currentTheme == Theme::Dark ? QStringLiteral("#7f8c99") : QStringLiteral("#666666");
    const auto html = QStringLiteral("<div style=\"color:%1\">[%2] <i>%3</i></div>").arg(systemColor, escapedTime, escapedText);
    m_chatView->append(html);
}

void MainWindow::renderAllMessages()
{
    m_chatView->clear();
    for (const auto &entry : m_chatHistory) {
        if (entry.isSystem) {
            const auto timeStamp = entry.timestamp.toLocalTime().toString("hh:mm:ss");
            const auto escapedTime = htmlEscape(timeStamp);
            const auto escapedText = htmlEscape(entry.text);
            const QString systemColor = m_currentTheme == Theme::Dark ? QStringLiteral("#7f8c99") : QStringLiteral("#666666");
            const auto html = QStringLiteral("<div style=\"color:%1\">[%2] <i>%3</i></div>").arg(systemColor, escapedTime, escapedText);
            m_chatView->append(html);
        } else {
            const auto timeStamp = entry.timestamp.toLocalTime().toString("hh:mm:ss");
            const auto escapedSender = htmlEscape(entry.sender);
            const auto escapedText = htmlEscape(entry.text);
            const auto escapedTime = htmlEscape(timeStamp);
            const bool isSelf = QString::compare(entry.sender, m_client->userName(), Qt::CaseInsensitive) == 0;
            
            QString senderStyle, textStyle, timeStyle;
            if (m_currentTheme == Theme::Dark) {
                timeStyle = QStringLiteral("color:#888888;");
                senderStyle = isSelf ? QStringLiteral("font-weight:600;color:#5fb8ff;") : QStringLiteral("font-weight:600;color:#ffffff;");
                textStyle = isSelf ? QStringLiteral("color:#d0ecff;") : QStringLiteral("color:#ffffff;");
            } else {
                timeStyle = QStringLiteral("color:#666666;");
                senderStyle = isSelf ? QStringLiteral("font-weight:600;color:#0066cc;") : QStringLiteral("font-weight:600;color:#000000;");
                textStyle = isSelf ? QStringLiteral("color:#003366;") : QStringLiteral("color:#000000;");
            }
            
            const auto html = QStringLiteral("<div><span style=\"%1\">[%2]</span> <span style=\"%3\">%4</span>: <span style=\"%5\">%6</span></div>")
                                  .arg(timeStyle, escapedTime, senderStyle, escapedSender, textStyle, escapedText);
            m_chatView->append(html);
        }
    }
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

void MainWindow::onThemeChanged()
{
    m_currentTheme = (m_currentTheme == Theme::Dark) ? Theme::Light : Theme::Dark;
    applyTheme(m_currentTheme);
    saveTheme();
}

void MainWindow::applyTheme(Theme theme)
{
    if (theme == Theme::Dark) {
        // Темная тема
        const QString darkStyle = QStringLiteral(
            "QWidget { background-color: #2b2b2b; color: #ffffff; }"
            "QTextEdit { background-color: #1e1e1e; color: #ffffff; border: 1px solid #3d3d3d; }"
            "QLineEdit { background-color: #3d3d3d; color: #ffffff; border: 1px solid #555555; padding: 3px; }"
            "QPushButton { background-color: #3d3d3d; color: #ffffff; border: 1px solid #555555; padding: 5px; }"
            "QPushButton:hover { background-color: #4d4d4d; }"
            "QPushButton:pressed { background-color: #2d2d2d; }"
            "QLabel { color: #ffffff; }"
            "QListWidget { background-color: #1e1e1e; color: #ffffff; border: 1px solid #3d3d3d; }"
            "QListWidget::item { padding: 3px; }"
            "QListWidget::item:selected { background-color: #3d3d3d; }"
        );
        m_centralWidget->setStyleSheet(darkStyle);
        m_chatView->setStyleSheet(QStringLiteral("QTextEdit { background-color: #1e1e1e; color: #ffffff; border: 1px solid #3d3d3d; }"));
        m_themeButton->setText(tr("Светлая"));
    } else {
        // Светлая тема
        const QString lightStyle = QStringLiteral(
            "QWidget { background-color: #f5f5f5; color: #000000; }"
            "QTextEdit { background-color: #ffffff; color: #000000; border: 1px solid #cccccc; }"
            "QLineEdit { background-color: #ffffff; color: #000000; border: 1px solid #cccccc; padding: 3px; }"
            "QPushButton { background-color: #e0e0e0; color: #000000; border: 1px solid #cccccc; padding: 5px; }"
            "QPushButton:hover { background-color: #d0d0d0; }"
            "QPushButton:pressed { background-color: #c0c0c0; }"
            "QLabel { color: #000000; }"
            "QListWidget { background-color: #ffffff; color: #000000; border: 1px solid #cccccc; }"
            "QListWidget::item { padding: 3px; }"
            "QListWidget::item:selected { background-color: #e0e0e0; }"
        );
        m_centralWidget->setStyleSheet(lightStyle);
        m_chatView->setStyleSheet(QStringLiteral("QTextEdit { background-color: #ffffff; color: #000000; border: 1px solid #cccccc; }"));
        m_themeButton->setText(tr("Темная"));
    }
    
    // Перерисовываем все сообщения с новыми цветами
    renderAllMessages();
}

void MainWindow::loadTheme()
{
    QSettings settings;
    const int themeValue = settings.value(QStringLiteral("theme"), static_cast<int>(Theme::Dark)).toInt();
    m_currentTheme = (themeValue == static_cast<int>(Theme::Light)) ? Theme::Light : Theme::Dark;
}

void MainWindow::saveTheme()
{
    QSettings settings;
    settings.setValue(QStringLiteral("theme"), static_cast<int>(m_currentTheme));
}


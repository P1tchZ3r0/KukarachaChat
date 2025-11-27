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
{
    // Создаем клиент для работы с сервером
    m_client = std::make_unique<ChatClient>(this);
    
    // Загружаем сохраненную тему
    loadTheme();
    
    // Строим интерфейс
    buildUi();
    
    // Подключаем сигналы к слотам
    bindSignals();
    
    // Применяем тему
    applyTheme(m_currentTheme);
    
    // Показываем приветственное сообщение
    appendSystemMessage(tr("Введите данные сервера и нажмите Подключиться"));
}

MainWindow::~MainWindow()
{
    // Деструктор - Qt сам удалит виджеты
}

void MainWindow::buildUi()
{
    // Создаем центральный виджет
    m_centralWidget = new QWidget(this);
    
    // Создаем главный layout (горизонтальный)
    QHBoxLayout *mainLayout = new QHBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Основная часть (чат)
    QWidget *chatWidget = new QWidget(m_centralWidget);
    QVBoxLayout *layout = new QVBoxLayout(chatWidget);

    // Layout для полей подключения
    QHBoxLayout *connectionLayout = new QHBoxLayout();
    
    // Поле для хоста
    QLabel *hostLabel = new QLabel(tr("Хост:"), chatWidget);
    connectionLayout->addWidget(hostLabel);
    m_hostEdit = new QLineEdit("127.0.0.1", chatWidget);
    connectionLayout->addWidget(m_hostEdit);

    // Поле для порта
    QLabel *portLabel = new QLabel(tr("Порт:"), chatWidget);
    connectionLayout->addWidget(portLabel);
    m_portEdit = new QLineEdit(QString::number(4242), chatWidget);
    m_portEdit->setMaximumWidth(80);
    connectionLayout->addWidget(m_portEdit);

    // Поле для имени пользователя
    QLabel *nameLabel = new QLabel(tr("Имя:"), chatWidget);
    connectionLayout->addWidget(nameLabel);
    m_userNameEdit = new QLineEdit("User", chatWidget);
    connectionLayout->addWidget(m_userNameEdit);

    // Поле для пароля
    QLabel *passLabel = new QLabel(tr("Пароль:"), chatWidget);
    connectionLayout->addWidget(passLabel);
    m_passwordEdit = new QLineEdit(chatWidget);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    connectionLayout->addWidget(m_passwordEdit);

    // Кнопка подключения
    m_connectButton = new QPushButton(tr("Подключиться"), chatWidget);
    connectionLayout->addWidget(m_connectButton);
    
    // Кнопка смены темы
    m_themeButton = new QPushButton(tr("Тема"), chatWidget);
    m_themeButton->setMaximumWidth(80);
    connectionLayout->addWidget(m_themeButton);
    
    // Растягиваем пространство
    connectionLayout->addStretch();

    layout->addLayout(connectionLayout);

    m_chatView = new QTextEdit(chatWidget);
    m_chatView->setReadOnly(true);
    layout->addWidget(m_chatView, 1);

    // Layout для ввода сообщения
    QHBoxLayout *messageLayout = new QHBoxLayout();
    m_messageEdit = new QLineEdit(chatWidget);
    m_messageEdit->setPlaceholderText(tr("Введите сообщение..."));
    messageLayout->addWidget(m_messageEdit, 1);

    // Кнопка отправки
    m_sendButton = new QPushButton(tr("Отправить"), chatWidget);
    messageLayout->addWidget(m_sendButton);

    layout->addLayout(messageLayout);

    // Панель со списком пользователей справа
    m_userListPanel = new QWidget(m_centralWidget);
    QVBoxLayout *userListLayout = new QVBoxLayout(m_userListPanel);
    userListLayout->setContentsMargins(5, 5, 5, 5);
    
    // Заголовок списка пользователей
    m_userListLabel = new QLabel(tr("Пользователи:"), m_userListPanel);
    userListLayout->addWidget(m_userListLabel);
    
    // Сам список пользователей
    m_userListWidget = new QListWidget(m_userListPanel);
    m_userListWidget->setMaximumWidth(200);
    m_userListWidget->setMinimumWidth(150);
    userListLayout->addWidget(m_userListWidget);

    // Размещаем основную часть и панель пользователей
    mainLayout->addWidget(chatWidget, 1);
    mainLayout->addWidget(m_userListPanel, 0);

    setCentralWidget(m_centralWidget);
    
    // Устанавливаем иконку окна
    QIcon windowIcon = QIcon::fromTheme("mail-message-new", QIcon::fromTheme("dialog-information"));
    if (windowIcon.isNull() == false) {
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
    // Проверяем подключение
    if (m_client->isConnected() == false || m_authenticated == false) {
        appendSystemMessage(tr("Сначала подключитесь к серверу"));
        return;
    }

    // Получаем текст из поля ввода
    QString text = m_messageEdit->text();
    QString trimmedText = text.trimmed();
    if (trimmedText.isEmpty()) {
        return;
    }

    // Отправляем сообщение
    m_client->sendMessage(text);
    
    // Очищаем поле ввода
    m_messageEdit->clear();
}

void MainWindow::onConnectClicked()
{
    // Если уже подключены, отключаемся
    if (m_client->isConnected()) {
        m_client->disconnectFromServer();
        return;
    }

    // Парсим порт
    bool ok = false;
    QString portText = m_portEdit->text();
    quint16 port = portText.toUShort(&ok);
    if (ok == false) {
        onErrorOccurred(tr("Некорректный порт"));
        return;
    }

    // Получаем данные для подключения
    QString host = m_hostEdit->text();
    QString name = m_userNameEdit->text();
    QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        onErrorOccurred(tr("Введите имя пользователя"));
        return;
    }

    QString password = m_passwordEdit->text();
    if (password.isEmpty()) {
        onErrorOccurred(tr("Введите пароль"));
        return;
    }

    // Подключаемся к серверу
    m_client->connectToServer(host, port, trimmedName, password);
}

void MainWindow::onMessageReceived(const ChatMessage &message)
{
    // Проверяем, системное ли это сообщение
    QString sender = message.sender();
    if (sender == "SERVER") {
        // Сохраняем в историю
        ChatEntry entry;
        entry.sender = "SERVER";
        entry.text = message.text();
        entry.timestamp = message.timestamp();
        entry.isSystem = true;
        m_chatHistory.append(entry);
        
        // Форматируем время
        QDateTime timestamp = message.timestamp();
        QDateTime localTime = timestamp.toLocalTime();
        QString timeStamp = localTime.toString("hh:mm:ss");
        QString escapedTime = htmlEscape(timeStamp);
        QString messageText = message.text();
        QString escapedText = htmlEscape(messageText);
        
        // Выбираем цвет в зависимости от темы
        QString systemColor;
        if (m_currentTheme == Theme::Dark) {
            systemColor = "#7f8c99";
        } else {
            systemColor = "#666666";
        }
        
        // Формируем HTML
        QString html = QString("<div style=\"color:%1\">[%2] <i>%3</i></div>").arg(systemColor, escapedTime, escapedText);
        m_chatView->append(html);
        return;
    }

    // Обычное сообщение от пользователя
    ChatEntry entry;
    entry.sender = message.sender();
    entry.text = message.text();
    entry.timestamp = message.timestamp();
    entry.isSystem = false;
    m_chatHistory.append(entry);

    // Форматируем время
    QDateTime timestamp = message.timestamp();
    QDateTime localTime = timestamp.toLocalTime();
    QString timeStamp = localTime.toString("hh:mm:ss");
    QString escapedTime = htmlEscape(timeStamp);
    
    // Экранируем текст
    QString senderName = message.sender();
    QString escapedSender = htmlEscape(senderName);
    QString messageText = message.text();
    QString escapedText = htmlEscape(messageText);
    
    // Проверяем, это наше сообщение или нет
    QString myName = m_client->userName();
    bool isSelf = (QString::compare(senderName, myName, Qt::CaseInsensitive) == 0);
    
    // Выбираем стили в зависимости от темы
    QString senderStyle;
    QString textStyle;
    QString timeStyle;
    
    if (m_currentTheme == Theme::Dark) {
        timeStyle = "color:#888888;";
        if (isSelf) {
            senderStyle = "font-weight:600;color:#5fb8ff;";
            textStyle = "color:#d0ecff;";
        } else {
            senderStyle = "font-weight:600;color:#ffffff;";
            textStyle = "color:#ffffff;";
        }
    } else {
        timeStyle = "color:#666666;";
        if (isSelf) {
            senderStyle = "font-weight:600;color:#0066cc;";
            textStyle = "color:#003366;";
        } else {
            senderStyle = "font-weight:600;color:#000000;";
            textStyle = "color:#000000;";
        }
    }
    
    // Формируем HTML для сообщения
    QString html = QString("<div><span style=\"%1\">[%2]</span> <span style=\"%3\">%4</span>: <span style=\"%5\">%6</span></div>")
                      .arg(timeStyle, escapedTime, senderStyle, escapedSender, textStyle, escapedText);
    m_chatView->append(html);
    
    // Показываем уведомление
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
    // Очищаем список
    m_userListWidget->clear();
    
    // Добавляем всех пользователей
    for (const QString &user : users) {
        m_userListWidget->addItem(user);
    }
    
    // Обновляем заголовок с количеством
    int userCount = users.size();
    QString labelText = tr("Пользователи (%1):").arg(userCount);
    m_userListLabel->setText(labelText);
}

void MainWindow::onErrorOccurred(const QString &message)
{
    appendSystemMessage(tr("Ошибка: %1").arg(message));
}

void MainWindow::appendSystemMessage(const QString &message)
{
    // Получаем текущее время
    QDateTime currentTime = QDateTime::currentDateTime();
    QString timeStamp = currentTime.toString("hh:mm:ss");
    QString escapedTime = htmlEscape(timeStamp);
    QString escapedText = htmlEscape(message);
    
    // Выбираем цвет в зависимости от темы
    QString systemColor;
    if (m_currentTheme == Theme::Dark) {
        systemColor = "#7f8c99";
    } else {
        systemColor = "#666666";
    }
    
    // Формируем HTML
    QString html = QString("<div style=\"color:%1\">[%2] <i>%3</i></div>").arg(systemColor, escapedTime, escapedText);
    m_chatView->append(html);
}

void MainWindow::renderAllMessages()
{
    // Очищаем чат
    m_chatView->clear();
    
    // Перерисовываем все сообщения из истории
    for (const ChatEntry &entry : m_chatHistory) {
        
        if (entry.isSystem) {
            // Системное сообщение
            QDateTime localTime = entry.timestamp.toLocalTime();
            QString timeStamp = localTime.toString("hh:mm:ss");
            QString escapedTime = htmlEscape(timeStamp);
            QString escapedText = htmlEscape(entry.text);
            
            QString systemColor;
            if (m_currentTheme == Theme::Dark) {
                systemColor = "#7f8c99";
            } else {
                systemColor = "#666666";
            }
            
            QString html = QString("<div style=\"color:%1\">[%2] <i>%3</i></div>").arg(systemColor, escapedTime, escapedText);
            m_chatView->append(html);
        } else {
            // Обычное сообщение
            QDateTime localTime = entry.timestamp.toLocalTime();
            QString timeStamp = localTime.toString("hh:mm:ss");
            QString escapedTime = htmlEscape(timeStamp);
            QString escapedSender = htmlEscape(entry.sender);
            QString escapedText = htmlEscape(entry.text);
            
            // Проверяем, наше ли это сообщение
            QString myName = m_client->userName();
            bool isSelf = (QString::compare(entry.sender, myName, Qt::CaseInsensitive) == 0);
            
            // Выбираем стили
            QString senderStyle;
            QString textStyle;
            QString timeStyle;
            
            if (m_currentTheme == Theme::Dark) {
                timeStyle = "color:#888888;";
                if (isSelf) {
                    senderStyle = "font-weight:600;color:#5fb8ff;";
                    textStyle = "color:#d0ecff;";
                } else {
                    senderStyle = "font-weight:600;color:#ffffff;";
                    textStyle = "color:#ffffff;";
                }
            } else {
                timeStyle = "color:#666666;";
                if (isSelf) {
                    senderStyle = "font-weight:600;color:#0066cc;";
                    textStyle = "color:#003366;";
                } else {
                    senderStyle = "font-weight:600;color:#000000;";
                    textStyle = "color:#000000;";
                }
            }
            
            QString html = QString("<div><span style=\"%1\">[%2]</span> <span style=\"%3\">%4</span>: <span style=\"%5\">%6</span></div>")
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
    // Проверяем состояние подключения
    bool connected = m_client->isConnected();
    bool enabled = connected && m_authenticated;
    
    // Обновляем кнопки и поля
    m_sendButton->setEnabled(enabled);
    m_messageEdit->setEnabled(enabled);
    
    // Меняем текст кнопки подключения
    if (connected) {
        m_connectButton->setText(tr("Отключиться"));
    } else {
        m_connectButton->setText(tr("Подключиться"));
    }
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
    // Переключаем тему
    if (m_currentTheme == Theme::Dark) {
        m_currentTheme = Theme::Light;
    } else {
        m_currentTheme = Theme::Dark;
    }
    
    // Применяем новую тему
    applyTheme(m_currentTheme);
    
    // Сохраняем выбор
    saveTheme();
}

void MainWindow::applyTheme(Theme theme)
{
    if (theme == Theme::Dark) {
        // Темная тема
        QString darkStyle = "QWidget { background-color: #2b2b2b; color: #ffffff; }"
                           "QTextEdit { background-color: #1e1e1e; color: #ffffff; border: 1px solid #3d3d3d; }"
                           "QLineEdit { background-color: #3d3d3d; color: #ffffff; border: 1px solid #555555; padding: 3px; }"
                           "QPushButton { background-color: #3d3d3d; color: #ffffff; border: 1px solid #555555; padding: 5px; }"
                           "QPushButton:hover { background-color: #4d4d4d; }"
                           "QPushButton:pressed { background-color: #2d2d2d; }"
                           "QLabel { color: #ffffff; }"
                           "QListWidget { background-color: #1e1e1e; color: #ffffff; border: 1px solid #3d3d3d; }"
                           "QListWidget::item { padding: 3px; }"
                           "QListWidget::item:selected { background-color: #3d3d3d; }";
        m_centralWidget->setStyleSheet(darkStyle);
        m_chatView->setStyleSheet("QTextEdit { background-color: #1e1e1e; color: #ffffff; border: 1px solid #3d3d3d; }");
        m_themeButton->setText(tr("Светлая"));
    } else {
        // Светлая тема
        QString lightStyle = "QWidget { background-color: #f5f5f5; color: #000000; }"
                            "QTextEdit { background-color: #ffffff; color: #000000; border: 1px solid #cccccc; }"
                            "QLineEdit { background-color: #ffffff; color: #000000; border: 1px solid #cccccc; padding: 3px; }"
                            "QPushButton { background-color: #e0e0e0; color: #000000; border: 1px solid #cccccc; padding: 5px; }"
                            "QPushButton:hover { background-color: #d0d0d0; }"
                            "QPushButton:pressed { background-color: #c0c0c0; }"
                            "QLabel { color: #000000; }"
                            "QListWidget { background-color: #ffffff; color: #000000; border: 1px solid #cccccc; }"
                            "QListWidget::item { padding: 3px; }"
                            "QListWidget::item:selected { background-color: #e0e0e0; }";
        m_centralWidget->setStyleSheet(lightStyle);
        m_chatView->setStyleSheet("QTextEdit { background-color: #ffffff; color: #000000; border: 1px solid #cccccc; }");
        m_themeButton->setText(tr("Темная"));
    }
    
    // Перерисовываем все сообщения с новыми цветами
    renderAllMessages();
}

void MainWindow::loadTheme()
{
    // Загружаем сохраненную тему из настроек
    QSettings settings;
    int defaultTheme = static_cast<int>(Theme::Dark);
    QVariant themeValue = settings.value("theme", defaultTheme);
    int themeInt = themeValue.toInt();
    
    if (themeInt == static_cast<int>(Theme::Light)) {
        m_currentTheme = Theme::Light;
    } else {
        m_currentTheme = Theme::Dark;
    }
}

void MainWindow::saveTheme()
{
    // Сохраняем текущую тему в настройки
    QSettings settings;
    int themeInt = static_cast<int>(m_currentTheme);
    settings.setValue("theme", themeInt);
}


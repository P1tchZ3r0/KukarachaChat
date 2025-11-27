#pragma once

#include <QMainWindow>
#include <QDateTime>
#include <QList>
#include <QString>

#include <memory>

class ChatClient;
class ChatMessage;
class QTextEdit;
class QLineEdit;
class QPushButton;
class QSystemTrayIcon;
class QListWidget;
class QLabel;
class QWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onSendClicked();
    void onConnectClicked();
    void onMessageReceived(const ChatMessage &message);
    void onConnectionStateChanged(bool connected);
    void onErrorOccurred(const QString &message);
    void onAuthenticatedChanged(bool authenticated);
    void onThemeChanged();

private:
    enum class Theme {
        Dark,
        Light
    };

    void buildUi();
    void bindSignals();
    void appendSystemMessage(const QString &message);
    void updateControls();
    void showMessageNotification(const ChatMessage &message);
    void applyTheme(Theme theme);
    void loadTheme();
    void saveTheme();
    static QString htmlEscape(const QString &text);

    struct ChatEntry {
        QString sender;
        QString text;
        QDateTime timestamp;
        bool isSystem;
    };

    void renderAllMessages();
    void updateUserList(const QStringList &users);

    std::unique_ptr<ChatClient> m_client;
    QWidget *m_centralWidget = nullptr;
    QTextEdit *m_chatView = nullptr;
    QLineEdit *m_messageEdit = nullptr;
    QLineEdit *m_userNameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QLineEdit *m_hostEdit = nullptr;
    QLineEdit *m_portEdit = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_themeButton = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QListWidget *m_userListWidget = nullptr;
    QLabel *m_userListLabel = nullptr;
    QWidget *m_userListPanel = nullptr;
    bool m_authenticated = false;
    Theme m_currentTheme = Theme::Dark;
    QList<ChatEntry> m_chatHistory;
};


#pragma once

#include <QMainWindow>

#include <memory>

class ChatClient;
class ChatMessage;
class QTextEdit;
class QLineEdit;
class QPushButton;
class QSystemTrayIcon;

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

private:
    void buildUi();
    void bindSignals();
    void appendSystemMessage(const QString &message);
    void updateControls();
    void showMessageNotification(const ChatMessage &message);

    std::unique_ptr<ChatClient> m_client;
    QTextEdit *m_chatView = nullptr;
    QLineEdit *m_messageEdit = nullptr;
    QLineEdit *m_userNameEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QLineEdit *m_hostEdit = nullptr;
    QLineEdit *m_portEdit = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_connectButton = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    bool m_authenticated = false;
};


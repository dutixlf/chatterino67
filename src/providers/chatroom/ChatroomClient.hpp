// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/websockets/WebSocketPool.hpp"
#include "providers/chatroom/ChatroomTypes.hpp"
#include "util/PostToThread.hpp"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <memory>
#include <unordered_set>

namespace chatterino::chatroom {

class ChatroomManager;
struct ShadowMessage;
struct GerrInfo;
struct GhostInfo;

/// WebSocket client for the shadow-chat server.
/// Owns a single WSS connection; all room subscriptions live on it.
/// All public methods must be called from the GUI thread.
class ChatroomClient : public QObject
{
    Q_OBJECT

public:
    explicit ChatroomClient(ChatroomManager &manager,
                            std::chrono::milliseconds heartbeatInterval);
    ~ChatroomClient() override;

    void connectToServer(const QString &ticket);

    void subscribeRoom(const QString &roomId);
    void unsubscribeRoom(const QString &roomId);
    void sendMessage(const QString &roomId, const QString &text,
                     const QString &parentId = {});
    void sendModAction(const QString &roomId, const QString &action,
                       const QString &target, int seconds = 0);
    void sendDelete(const QString &roomId, const QString &messageId);
    void sendRoomState(const QString &roomId, bool enabled);
    void sendSelfState(const QString &roomId, bool vip);
    void close();
    bool isConnected() const;
    bool isVerified() const;

Q_SIGNALS:
    void connected();
    void disconnected();
    void verifiedChanged(bool verified);
    void messageReceived(const QString &roomId, const ShadowMessage &msg);
    void historyReceived(const QString &roomId, const GhostInfo &ghost);
    void modActionReceived(const QString &roomId, const QString &action,
                           const QString &userId, qint64 until);
    void messageDeleted(const QString &roomId, const QString &messageId);
    void roomStateChanged(const QString &roomId, bool enabled);
    void errorReceived(const QString &roomId, const GerrInfo &err);
    void authStale(const QString &roomId, bool needsImmediateRenew);

private Q_SLOTS:
    void onWsOpen();
    void onWsTextMessage(const QByteArray &msg);
    void onWsClose();

private:
    void sendJson(const QJsonObject &obj);
    void resubscribeAll();
    void handleHello(const QJsonObject &json);
    void handleGmsg(const QJsonObject &json);
    void handleGhist(const QJsonObject &json);
    void handleGban(const QJsonObject &json);
    void handleGunban(const QJsonObject &json);
    void handleGdel(const QJsonObject &json);
    void handleGstate(const QJsonObject &json);
    void handleGerr(const QJsonObject &json);
    void handleAuthstale(const QJsonObject &json);
    void handleHeartbeat(const QJsonObject &json);

    // ── internal WebSocket listener (runs in ws thread, posts to GUI) ─

    class Listener : public WebSocketListener
    {
    public:
        Listener(QPointer<ChatroomClient> parent)
            : parent_(std::move(parent))
        {
        }

        void onOpen() override
        {
            runInGuiThread([parent = this->parent_] {
                if (parent)
                    parent->onWsOpen();
            });
        }

        void onTextMessage(QByteArray data) override
        {
            runInGuiThread([parent = this->parent_, data] {
                if (parent)
                    parent->onWsTextMessage(data);
            });
        }

        void onBinaryMessage(QByteArray) override
        {
        }

        void onClose(std::unique_ptr<WebSocketListener> /*self*/) override
        {
            runInGuiThread([parent = this->parent_] {
                if (parent)
                    parent->onWsClose();
            });
        }

    private:
        QPointer<ChatroomClient> parent_;
    };

    ChatroomManager &manager_;
    std::optional<WebSocketPool> pool_;

    WebSocketHandle ws_;
    std::unordered_set<QString> subscribedRooms_;
    QSet<QString> pendingRooms_;

    bool verified_ = false;
    bool helloSent_ = false;
    bool connected_ = false;
    QString ticket_;

    std::chrono::milliseconds hbInterval_;
    QTimer heartbeatWatchdog_;
    /// Re-verify watchdog: if hello gets no response in time, the session
    /// is considered lost and the socket is closed (manager reconnects).
    QTimer helloWatchdog_;
};

} // namespace chatterino::chatroom

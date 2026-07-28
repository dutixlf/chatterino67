// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/chatroom/ChatroomTypes.hpp"
#include "providers/chatroom/TicketClient.hpp"

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>

namespace chatterino {

class TwitchAccount;
class TwitchChannel;

namespace chatroom {

class ChatroomClient;
struct ShadowMessage;
struct GhostInfo;
struct GerrInfo;

/// Facade for the shadow-chat feature.
///
/// Owns the TicketClient and ChatroomClient, manages room subscriptions
/// with reference counting, routes messages into TwitchChannels, and
/// provides the three-way send result (Normal/ShadowSent/ShadowFailed).
///
/// All public methods must be called from the GUI thread.
class ChatroomManager : public QObject
{
    Q_OBJECT

public:
    ChatroomManager();
    ~ChatroomManager() override;

    // ── lifecycle ────────────────────────────────────────────────────

    /// Start or restart the connection using the current account.
    void start();

    /// Stop everything and release resources.
    void stop();

    // ── room management ──────────────────────────────────────────────

    /// Subscribe to a room (twitch channel id).  Thread-safe via refcount.
    void subscribeRoom(const QString &roomId);

    /// Unsubscribe from a room.  Only drops the actual subscription when
    /// the refcount reaches zero.
    void unsubscribeRoom(const QString &roomId);
    bool isSubscribed(const QString &roomId) const;

    // ── sending ──────────────────────────────────────────────────────

    /// Three-way send: returns Normal, ShadowSent, or ShadowFailed (§8.3).
    ///
    /// Caller must NEVER forward a ShadowFailed text to Twitch.
    SendResult sendMessage(const QString &roomId, const QString &text,
                           const QString &parentId = {});
    void sendSelfState(const QString &roomId, bool vip);

    /// Convenience: returns true if the predicate for "send to shadow"
    /// is active.  Used by both routing and client-side filter gating (§8.7).
    bool shouldSendToShadow() const;

Q_SIGNALS:
    /// Emitted when a new message needs to be displayed in a channel.
    void displayMessageRequired(const QString &roomId,
                                const ShadowMessage &msg);

    /// Emitted when history arrives.
    void displayHistoryRequired(const QString &roomId,
                                const GhostInfo &ghost);

    /// Mod actions that affect rendering (ban/unban/delete).
    void modUser(const QString &roomId, const QString &userId,
                 qint64 until);
    void unmodUser(const QString &roomId, const QString &userId);
    void deleteMessage(const QString &roomId, const QString &messageId);

    /// Room enabled/disabled.
    void roomState(const QString &roomId, bool enabled);

    /// Error feedback for the user.
    void sendError(const QString &roomId, const QString &code,
                   double waitSeconds, qint64 banLeft);

    /// Connection state changes.
    void connectionStateChanged(bool connected, bool verified);

private Q_SLOTS:
    void onTicketReady(const QString &ticket, const QString &login,
                       const QString &displayName, bool scoped);
    void onTicketFailed(const QString &error);
    void onVerifiedChanged(bool verified);
    void onDisconnected();
    void onAuthStale(const QString &roomId, bool needsImmediate);
    void onMessageReceived(const QString &roomId,
                           const ShadowMessage &msg);

private:
    void scheduleReconnect();
    void doReconnect();

    std::unique_ptr<TicketClient> ticketClient_;
    std::unique_ptr<ChatroomClient> client_;

    // refcounted room subscriptions
    QHash<QString, int> roomRefs_;

    // Reconnection
    QTimer reconnectTimer_;
    int reconnectAttempt_ = 0;

    // Current state
    bool enabled_ = false;
    bool verified_ = false;
    bool connecting_ = false;
};

} // namespace chatroom
} // namespace chatterino

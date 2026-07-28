// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/chatroom/ChatroomManager.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/chatroom/ChatroomClient.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "singletons/Settings.hpp"

#include <chrono>

namespace chatterino::chatroom {

using namespace std::chrono_literals;

ChatroomManager::ChatroomManager()
    : ticketClient_(std::make_unique<TicketClient>())
    , client_(std::make_unique<ChatroomClient>(*this, 25s))
{
    // Wire ticket signals
    connect(this->ticketClient_.get(), &TicketClient::ticketReady, this,
            &ChatroomManager::onTicketReady);
    connect(this->ticketClient_.get(), &TicketClient::ticketFailed, this,
            &ChatroomManager::onTicketFailed);

    // Wire client signals
    connect(this->client_.get(), &ChatroomClient::verifiedChanged, this,
            &ChatroomManager::onVerifiedChanged);
    connect(this->client_.get(), &ChatroomClient::disconnected, this,
            &ChatroomManager::onDisconnected);
    connect(this->client_.get(), &ChatroomClient::authStale, this,
            &ChatroomManager::onAuthStale);

    // Incoming messages — filter then emit for display
    connect(this->client_.get(), &ChatroomClient::messageReceived, this,
            &ChatroomManager::onMessageReceived);
    connect(this->client_.get(), &ChatroomClient::historyReceived, this,
            &ChatroomManager::displayHistoryRequired);
    connect(this->client_.get(), &ChatroomClient::modActionReceived, this,
            [this](const QString &roomId, const QString &action,
                   const QString &userId, qint64 until) {
                if (action == QStringLiteral("unban"))
                {
                    Q_EMIT this->unmodUser(roomId, userId);
                }
                else
                {
                    Q_EMIT this->modUser(roomId, userId, until);
                }
            });
    connect(this->client_.get(), &ChatroomClient::messageDeleted, this,
            &ChatroomManager::deleteMessage);
    connect(this->client_.get(), &ChatroomClient::roomStateChanged, this,
            &ChatroomManager::roomState);
    connect(this->client_.get(), &ChatroomClient::errorReceived, this,
            [this](const QString &roomId, const GerrInfo &gerr) {
                Q_EMIT this->sendError(roomId, gerr.code, gerr.wait, gerr.left);
            });

    // Reconnection timer
    this->reconnectTimer_.setSingleShot(true);
    connect(&this->reconnectTimer_, &QTimer::timeout, this,
            &ChatroomManager::doReconnect);
}

ChatroomManager::~ChatroomManager()
{
    this->stop();
}

void ChatroomManager::start()
{
    qCDebug(chatterinoChatroom) << "ChatroomManager: start";

    auto account = getApp()->getAccounts()->twitch.getCurrent();
    if (!account || account->isAnon())
    {
        qCDebug(chatterinoChatroom)
            << "ChatroomManager: not logged in, cannot start";
        return;
    }

    this->enabled_ = true;
    this->connecting_ = true;

    this->ticketClient_->fetch(account);
}

void ChatroomManager::stop()
{
    qCDebug(chatterinoChatroom) << "ChatroomManager: stop";

    this->enabled_ = false;
    this->connecting_ = false;
    this->verified_ = false;
    this->reconnectTimer_.stop();

    this->client_->close();
    this->ticketClient_->clearCache();
}

void ChatroomManager::subscribeRoom(const QString &roomId)
{
    auto it = this->roomRefs_.find(roomId);
    if (it != this->roomRefs_.end())
    {
        it.value()++;
        qCDebug(chatterinoChatroom)
            << "ChatroomManager: refcount for" << roomId << "now" << it.value();
        return;
    }

    this->roomRefs_.insert(roomId, 1);
    this->client_->subscribeRoom(roomId);
    qCDebug(chatterinoChatroom) << "ChatroomManager: subscribed to" << roomId;
}

void ChatroomManager::unsubscribeRoom(const QString &roomId)
{
    auto it = this->roomRefs_.find(roomId);
    if (it == this->roomRefs_.end())
    {
        return;
    }

    it.value()--;
    qCDebug(chatterinoChatroom)
        << "ChatroomManager: refcount for" << roomId << "now" << it.value();

    if (it.value() <= 0)
    {
        this->roomRefs_.erase(it);
        this->client_->unsubscribeRoom(roomId);
        qCDebug(chatterinoChatroom)
            << "ChatroomManager: unsubscribed from" << roomId;
    }
}

SendResult ChatroomManager::sendMessage(const QString &roomId,
                                        const QString &text,
                                        const QString &parentId)
{
    if (!this->shouldSendToShadow())
    {
        return SendResult::Normal;
    }

    if (!this->verified_)
    {
        qCWarning(chatterinoChatroom)
            << "ChatroomManager: not verified, send failed";
        return SendResult::ShadowFailed;
    }

    if (!this->roomRefs_.contains(roomId))
    {
        qCWarning(chatterinoChatroom)
            << "ChatroomManager: room" << roomId << "not subscribed";
        return SendResult::ShadowFailed;
    }

    this->client_->sendMessage(roomId, text, parentId);

    return SendResult::ShadowSent;
}

void ChatroomManager::sendSelfState(const QString &roomId, bool vip)
{
    if (!this->verified_ || !this->roomRefs_.contains(roomId))
    {
        return;
    }
    this->client_->sendSelfState(roomId, vip);
}

bool ChatroomManager::shouldSendToShadow() const
{
    // §8.7 — one common predicate for routing, filters, and input indicator
    return getSettings()->shadowChatEnabled &&
           getSettings()->shadowChatSendToShadow && this->verified_;
}

// ── private slots ───────────────────────────────────────────────────

void ChatroomManager::onTicketReady(const QString &ticket,
                                     const QString &login,
                                     const QString & /*displayName*/,
                                     bool /*scoped*/)
{
    qCDebug(chatterinoChatroom)
        << "ChatroomManager: ticket ready for" << login;

    this->connecting_ = false;
    this->client_->connectToServer(ticket);
}

void ChatroomManager::onTicketFailed(const QString &error)
{
    qCWarning(chatterinoChatroom)
        << "ChatroomManager: ticket failed:" << error;

    this->connecting_ = false;

    if (this->enabled_)
    {
        this->scheduleReconnect();
    }
}

void ChatroomManager::onVerifiedChanged(bool verified)
{
    this->verified_ = verified;

    qCDebug(chatterinoChatroom)
        << "ChatroomManager: verified =" << verified;

    Q_EMIT connectionStateChanged(this->client_->isConnected(), verified);

    if (verified)
    {
        // Reset exponential backoff on successful verification
        this->reconnectAttempt_ = 0;
        return;
    }

    if (this->enabled_)
    {
        // Not verified — the server rejected our ticket. Re-fetch and reconnect.
        qCDebug(chatterinoChatroom)
            << "ChatroomManager: re-fetching ticket after rejected verification";
        this->ticketClient_->forceFetch(
            getApp()->getAccounts()->twitch.getCurrent());
    }
}

void ChatroomManager::onDisconnected()
{
    qCDebug(chatterinoChatroom)
        << "ChatroomManager: disconnected, will reconnect";
    this->verified_ = false;
    Q_EMIT connectionStateChanged(false, false);

    if (this->enabled_)
    {
        this->scheduleReconnect();
    }
}

void ChatroomManager::onAuthStale(const QString & /*roomId*/,
                                   bool needsImmediate)
{
    qCDebug(chatterinoChatroom)
        << "ChatroomManager: authstale (immediate =" << needsImmediate << ")";

    // §6: reason:"stale" means re-fetch IMMEDIATELY, bypassing own throttles
    if (needsImmediate)
    {
        this->reconnectTimer_.stop();
        this->ticketClient_->forceFetch(
            getApp()->getAccounts()->twitch.getCurrent());
    }
    else
    {
        // Non-stale authstale — just note it, fetch on next opportunity
        if (this->ticketClient_)
        {
            // passive refresh happens via the timer in TicketClient anyway
        }
    }
}

void ChatroomManager::scheduleReconnect()
{
    if (this->connecting_ || !this->enabled_)
    {
        return;
    }

    this->connecting_ = true;

    // Exponential backoff: 1s, 2s, 4s, 8s, 16s, cap at 30s
    int delay = std::min(1000 * (1 << this->reconnectAttempt_), 30000);
    this->reconnectAttempt_++;

    qCDebug(chatterinoChatroom)
        << "ChatroomManager: reconnect in" << delay << "ms (attempt"
        << this->reconnectAttempt_ << ")";

    this->reconnectTimer_.start(delay);
}

void ChatroomManager::onMessageReceived(const QString &roomId,
                                         const ShadowMessage &msg)
{
    // §8.4: server echoes our own sent messages back.  This echo IS the
    // canonical display copy (we skipped the optimistic local add in
    // TwitchChannel::sendMessage).  §7.1: future per-setting filtering
    // (own-msg, ignore list, etc.) goes here.
    Q_EMIT displayMessageRequired(roomId, msg);
}

void ChatroomManager::doReconnect()
{
    qCDebug(chatterinoChatroom) << "ChatroomManager: reconnecting";
    this->connecting_ = false;
    this->start();
}

} // namespace chatterino::chatroom

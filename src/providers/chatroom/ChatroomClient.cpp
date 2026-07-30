// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/chatroom/ChatroomClient.hpp"

#include "common/QLogging.hpp"
#include "common/websockets/WebSocketPool.hpp"
#include "providers/chatroom/ChatroomManager.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace chatterino::chatroom {

ChatroomClient::ChatroomClient(ChatroomManager &manager,
                               std::chrono::milliseconds heartbeatInterval)
    : manager_(manager)
    , pool_(std::make_optional<WebSocketPool>(QStringLiteral("chatroom")))
    , hbInterval_(heartbeatInterval)
{
    this->heartbeatWatchdog_.setInterval(heartbeatInterval * 3);
    this->heartbeatWatchdog_.setSingleShot(true);
    connect(&this->heartbeatWatchdog_, &QTimer::timeout, this,
            [this]() {
                qCWarning(chatterinoChatroom)
                    << "ChatroomClient: heartbeat timeout, closing";
                this->close();
            });

    this->helloWatchdog_.setInterval(std::chrono::seconds(10));
    this->helloWatchdog_.setSingleShot(true);
    connect(&this->helloWatchdog_, &QTimer::timeout, this, [this]() {
        if (!this->verified_)
        {
            qCWarning(chatterinoChatroom)
                << "ChatroomClient: hello timeout, verification lost, closing";
            this->close();
        }
    });
}

ChatroomClient::~ChatroomClient()
{
    this->close();
}

void ChatroomClient::connectToServer(const QString &ticket)
{
    if (this->connected_)
    {
        qCDebug(chatterinoChatroom)
            << "ChatroomClient: already connected, re-sending hello";
        this->ticket_ = ticket;
        this->helloSent_ = false;
        this->verified_ = false;
        // Queue current rooms for resubscription after re-verification
        for (const auto &r : this->subscribedRooms_)
        {
            this->pendingRooms_.insert(r);
        }
        this->subscribedRooms_.clear();
        this->sendJson({
            {QStringLiteral("op"), QStringLiteral("hello")},
            {QStringLiteral("ticket"), ticket},
        });
        this->helloSent_ = true;
        this->helloWatchdog_.start();
        return;
    }

    if (!this->pool_)
    {
        qCWarning(chatterinoChatroom)
            << "ChatroomClient: pool destroyed, cannot connect";
        return;
    }

    qCDebug(chatterinoChatroom)
        << "ChatroomClient: connecting to wss://ws.rish098.xyz/";

    this->ticket_ = ticket;
    this->helloSent_ = false;
    this->verified_ = false;

    this->ws_ = this->pool_->createSocket(
        WebSocketOptions{
            .url = QStringLiteral("wss://ws.rish098.xyz/"),
        },
        std::make_unique<Listener>(this));
}

void ChatroomClient::subscribeRoom(const QString &roomId)
{
    if (!this->verified_)
    {
        this->pendingRooms_.insert(roomId);
        qCDebug(chatterinoChatroom)
            << "ChatroomClient: queued sub for" << roomId
            << "(not yet verified)";
        return;
    }

    if (this->subscribedRooms_.contains(roomId))
    {
        return;
    }

    this->subscribedRooms_.insert(roomId);
    this->sendJson({
        {QStringLiteral("op"), QStringLiteral("gsub")},
        {QStringLiteral("room"), roomId},
    });
    qCDebug(chatterinoChatroom) << "ChatroomClient: subscribed to" << roomId;
}

void ChatroomClient::unsubscribeRoom(const QString &roomId)
{
    this->pendingRooms_.remove(roomId);
    if (!this->subscribedRooms_.erase(roomId))
    {
        return;
    }

    this->sendJson({
        {QStringLiteral("op"), QStringLiteral("gunsub")},
        {QStringLiteral("room"), roomId},
    });
    qCDebug(chatterinoChatroom) << "ChatroomClient: unsubscribed from"
                                << roomId;
}

void ChatroomClient::sendMessage(const QString &roomId, const QString &text,
                                 const QString &parentId)
{
    if (!this->verified_)
    {
        qCWarning(chatterinoChatroom)
            << "ChatroomClient: cannot send, not verified";
        return;
    }

    if (!this->subscribedRooms_.contains(roomId))
    {
        qCWarning(chatterinoChatroom)
            << "ChatroomClient: cannot send, not subscribed to" << roomId;
        return;
    }

    QJsonObject obj{
        {QStringLiteral("op"), QStringLiteral("gmsg")},
        {QStringLiteral("room"), roomId},
        {QStringLiteral("text"), text},
    };
    if (!parentId.isEmpty())
    {
        obj[QStringLiteral("p")] = parentId;
    }
    this->sendJson(obj);
    qCDebug(chatterinoChatroom) << "ChatroomClient: sent gmsg to" << roomId;
}

void ChatroomClient::sendModAction(const QString &roomId,
                                    const QString &action,
                                    const QString &target, int seconds)
{
    if (!this->verified_)
    {
        return;
    }

    QJsonObject obj;
    obj[QStringLiteral("op")] = QStringLiteral("gmod");
    obj[QStringLiteral("room")] = roomId;
    obj[QStringLiteral("action")] = action;
    obj[QStringLiteral("target")] = target;
    if (seconds > 0)
    {
        obj[QStringLiteral("seconds")] = seconds;
    }
    this->sendJson(obj);
    qCDebug(chatterinoChatroom)
        << "ChatroomClient: gmod" << action << "on" << target;
}

void ChatroomClient::sendDelete(const QString &roomId,
                                 const QString &messageId)
{
    if (!this->verified_)
    {
        return;
    }

    this->sendJson({
        {QStringLiteral("op"), QStringLiteral("gmod")},
        {QStringLiteral("room"), roomId},
        {QStringLiteral("action"), QStringLiteral("delete")},
        {QStringLiteral("id"), messageId},
    });
    qCDebug(chatterinoChatroom)
        << "ChatroomClient: gmod delete" << messageId;
}

void ChatroomClient::sendSelfState(const QString &roomId, bool vip)
{
    if (!this->verified_)
    {
        return;
    }

    this->sendJson({
        {QStringLiteral("op"), QStringLiteral("gself")},
        {QStringLiteral("room"), roomId},
        {QStringLiteral("vip"), vip},
    });
    qCDebug(chatterinoChatroom)
        << "ChatroomClient: gself" << (vip ? "vip" : "!vip") << "in" << roomId;
}

void ChatroomClient::sendRoomState(const QString &roomId, bool enabled)
{
    if (!this->verified_)
    {
        return;
    }

    this->sendJson({
        {QStringLiteral("op"), QStringLiteral("groom")},
        {QStringLiteral("room"), roomId},
        {QStringLiteral("enabled"), enabled},
    });
    qCDebug(chatterinoChatroom)
        << "ChatroomClient: groom" << roomId << enabled;
}

void ChatroomClient::close()
{
    this->heartbeatWatchdog_.stop();
    this->helloWatchdog_.stop();
    this->ws_.close();
    // pool stays alive in case we reconnect
}

bool ChatroomClient::isConnected() const
{
    return this->connected_;
}

bool ChatroomClient::isVerified() const
{
    return this->verified_;
}

// ── private helpers ──────────────────────────────────────────────────

void ChatroomClient::sendJson(const QJsonObject &obj)
{
    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    qCDebug(chatterinoChatroom) << "ChatroomClient: >>" << data;
    this->ws_.sendText(data);
}

void ChatroomClient::resubscribeAll()
{
    for (const auto &roomId : this->subscribedRooms_)
    {
        this->sendJson({
            {QStringLiteral("op"), QStringLiteral("gsub")},
            {QStringLiteral("room"), roomId},
        });
    }
    qCDebug(chatterinoChatroom)
        << "ChatroomClient: resubscribed to"
        << this->subscribedRooms_.size() << "rooms";
}

void ChatroomClient::onWsOpen()
{
    qCDebug(chatterinoChatroom) << "ChatroomClient: socket opened, sending hello";

    this->connected_ = true;
    this->helloSent_ = false;
    if (!this->ticket_.isEmpty())
    {
        this->sendJson({
            {QStringLiteral("op"), QStringLiteral("hello")},
            {QStringLiteral("ticket"), this->ticket_},
        });
        this->helloSent_ = true;
        this->helloWatchdog_.start();
    }

    Q_EMIT connected();
}

void ChatroomClient::onWsTextMessage(const QByteArray &msg)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(msg, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
    {
        qCWarning(chatterinoChatroom)
            << "ChatroomClient: invalid JSON" << err.errorString();
        return;
    }

    QJsonObject json = doc.object();
    QString op = json.value(QStringLiteral("op")).toString();

    qCDebug(chatterinoChatroom) << "ChatroomClient: << op =" << op;

    if (op == QStringLiteral("hello"))
    {
        this->handleHello(json);
    }
    else if (op == QStringLiteral("gmsg"))
    {
        this->handleGmsg(json);
    }
    else if (op == QStringLiteral("ghist"))
    {
        this->handleGhist(json);
    }
    else if (op == QStringLiteral("gban"))
    {
        this->handleGban(json);
    }
    else if (op == QStringLiteral("gunban"))
    {
        this->handleGunban(json);
    }
    else if (op == QStringLiteral("gdel"))
    {
        this->handleGdel(json);
    }
    else if (op == QStringLiteral("gstate"))
    {
        this->handleGstate(json);
    }
    else if (op == QStringLiteral("gerr"))
    {
        this->handleGerr(json);
    }
    else if (op == QStringLiteral("authstale"))
    {
        this->handleAuthstale(json);
    }
    else if (op == QStringLiteral("heartbeat"))
    {
        this->handleHeartbeat(json);
    }
    else
    {
        qCDebug(chatterinoChatroom) << "ChatroomClient: unknown op" << op;
    }
}

void ChatroomClient::onWsClose()
{
    qCDebug(chatterinoChatroom) << "ChatroomClient: socket closed";

    this->connected_ = false;
    this->heartbeatWatchdog_.stop();
    this->helloWatchdog_.stop();
    this->verified_ = false;
    this->helloSent_ = false;

    for (const auto &r : this->subscribedRooms_)
    {
        this->pendingRooms_.insert(r);
    }
    this->subscribedRooms_.clear();

    Q_EMIT disconnected();
    Q_EMIT verifiedChanged(false);
}

// ── message handlers ────────────────────────────────────────────────

void ChatroomClient::handleHello(const QJsonObject &json)
{
    bool verified = json.value(QStringLiteral("verified")).toBool();
    int heartbeat = json.value(QStringLiteral("heartbeat")).toInt(25);

    this->helloWatchdog_.stop();

    qCDebug(chatterinoChatroom)
        << "ChatroomClient: hello response: verified =" << verified
        << "heartbeat =" << heartbeat;

    this->verified_ = verified;
    this->heartbeatWatchdog_.setInterval(
        std::chrono::seconds(std::max(heartbeat, 5) * 3));
    this->heartbeatWatchdog_.start();

    Q_EMIT verifiedChanged(verified);

    if (!verified)
    {
        qCWarning(chatterinoChatroom)
            << "ChatroomClient: not verified, manager will re-fetch ticket";
        return;
    }

    for (const auto &roomId : this->pendingRooms_)
    {
        this->subscribedRooms_.insert(roomId);
        this->sendJson({
            {QStringLiteral("op"), QStringLiteral("gsub")},
            {QStringLiteral("room"), roomId},
        });
        qCDebug(chatterinoChatroom)
            << "ChatroomClient: subscribed pending" << roomId;
    }
    this->pendingRooms_.clear();
}

void ChatroomClient::handleGmsg(const QJsonObject &json)
{
    ShadowMessage sm;
    sm.id = json.value(QStringLiteral("id")).toString();
    sm.ts = static_cast<qint64>(
        json.value(QStringLiteral("ts")).toDouble());
    sm.login = json.value(QStringLiteral("l")).toString();
    sm.userId = json.value(QStringLiteral("u")).toString();
    sm.displayName = json.value(QStringLiteral("n")).toString();
    sm.mod = json.value(QStringLiteral("mod")).toBool();
    sm.vip = json.value(QStringLiteral("vip")).toBool();
    sm.parentId = json.value(QStringLiteral("p")).toString();
    sm.text = json.value(QStringLiteral("text")).toString();

    QString roomId = json.value(QStringLiteral("room")).toString();

    qCDebug(chatterinoChatroom)
        << "ChatroomClient: gmsg from" << sm.login << "in" << roomId;

    Q_EMIT messageReceived(roomId, sm);
}

void ChatroomClient::handleGhist(const QJsonObject &json)
{
    GhostInfo ghost;
    ghost.roomId = json.value(QStringLiteral("room")).toString();
    ghost.enabled = json.value(QStringLiteral("enabled")).toBool();
    ghost.banned = json.value(QStringLiteral("banned")).toInt();
    ghost.mod = json.value(QStringLiteral("mod")).toBool();

    QJsonArray msgs = json.value(QStringLiteral("msgs")).toArray();
    for (const auto &m : msgs)
    {
        QJsonObject mo = m.toObject();
        ShadowMessage sm;
        sm.id = mo.value(QStringLiteral("id")).toString();
        sm.ts = static_cast<qint64>(mo.value(QStringLiteral("ts")).toDouble());
        sm.login = mo.value(QStringLiteral("l")).toString();
        sm.userId = mo.value(QStringLiteral("u")).toString();
        sm.displayName = mo.value(QStringLiteral("n")).toString();
        sm.mod = mo.value(QStringLiteral("mod")).toBool();
        sm.vip = mo.value(QStringLiteral("vip")).toBool();
        sm.parentId = mo.value(QStringLiteral("p")).toString();
        sm.text = mo.value(QStringLiteral("text")).toString();
        ghost.msgs.push_back(std::move(sm));
    }

    qCDebug(chatterinoChatroom)
        << "ChatroomClient: ghist for" << ghost.roomId
        << "msgs:" << ghost.msgs.size()
        << "enabled:" << ghost.enabled
        << "banned:" << ghost.banned;

    Q_EMIT historyReceived(ghost.roomId, ghost);
}

void ChatroomClient::handleGban(const QJsonObject &json)
{
    QString roomId = json.value(QStringLiteral("room")).toString();
    QString userId = json.value(QStringLiteral("u")).toString();
    qint64 until = static_cast<qint64>(
        json.value(QStringLiteral("until")).toDouble());

    qCDebug(chatterinoChatroom)
        << "ChatroomClient: gban" << userId << "in" << roomId
        << "until" << until;

    Q_EMIT modActionReceived(roomId, QStringLiteral("ban"), userId, until);
}

void ChatroomClient::handleGunban(const QJsonObject &json)
{
    QString roomId = json.value(QStringLiteral("room")).toString();
    QString userId = json.value(QStringLiteral("u")).toString();

    qCDebug(chatterinoChatroom)
        << "ChatroomClient: gunban" << userId << "in" << roomId;

    Q_EMIT modActionReceived(roomId, QStringLiteral("unban"), userId, 0);
}

void ChatroomClient::handleGdel(const QJsonObject &json)
{
    QString roomId = json.value(QStringLiteral("room")).toString();
    QString id = json.value(QStringLiteral("id")).toString();

    qCDebug(chatterinoChatroom)
        << "ChatroomClient: gdel" << id << "in" << roomId;

    Q_EMIT messageDeleted(roomId, id);
}

void ChatroomClient::handleGstate(const QJsonObject &json)
{
    QString roomId = json.value(QStringLiteral("room")).toString();
    bool enabled = json.value(QStringLiteral("enabled")).toBool();

    qCDebug(chatterinoChatroom)
        << "ChatroomClient: gstate" << roomId << "enabled:" << enabled;

    Q_EMIT roomStateChanged(roomId, enabled);
}

void ChatroomClient::handleGerr(const QJsonObject &json)
{
    GerrInfo ger;
    ger.code = json.value(QStringLiteral("code")).toString();
    ger.wait = json.value(QStringLiteral("wait")).toDouble(0.0);
    ger.left = static_cast<qint64>(
        json.value(QStringLiteral("left")).toDouble(0.0));

    QString roomId = json.value(QStringLiteral("room")).toString();

    qCDebug(chatterinoChatroom)
        << "ChatroomClient: gerr" << ger.code << "in" << roomId;

    Q_EMIT errorReceived(roomId, ger);
}

void ChatroomClient::handleAuthstale(const QJsonObject &json)
{
    QString roomId = json.value(QStringLiteral("room")).toString();
    QString reason = json.value(QStringLiteral("reason")).toString();
    bool needsImmediate = (reason == QStringLiteral("stale"));

    qCDebug(chatterinoChatroom)
        << "ChatroomClient: authstale" << reason;

    Q_EMIT authStale(roomId, needsImmediate);
}

void ChatroomClient::handleHeartbeat(const QJsonObject & /*json*/)
{
    this->heartbeatWatchdog_.start();
}

} // namespace chatterino::chatroom

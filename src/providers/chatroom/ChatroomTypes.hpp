// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArray>
#include <QDebug>
#include <QHash>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <unordered_set>

namespace chatterino::chatroom {

// ── Subscription type for BasicPubSubClient ──────────────────────────

struct RoomSubscription {
    QString roomId;

    QByteArray encodeSubscribe() const
    {
        QJsonObject obj;
        obj["op"] = QStringLiteral("gsub");
        obj["room"] = this->roomId;
        return QJsonDocument(obj).toJson(QJsonDocument::Compact);
    }

    QByteArray encodeUnsubscribe() const
    {
        QJsonObject obj;
        obj["op"] = QStringLiteral("gunsub");
        obj["room"] = this->roomId;
        return QJsonDocument(obj).toJson(QJsonDocument::Compact);
    }

    friend QDebug &operator<<(QDebug &dbg, const RoomSubscription &s)
    {
        dbg << "RoomSubscription(" << s.roomId << ")";
        return dbg;
    }

    bool operator==(const RoomSubscription &rhs) const
    {
        return this->roomId == rhs.roomId;
    }
};

// ── Three-way send result (§8.3) ────────────────────────────────────

enum class SendResult {
    Normal,       // shadow chat not engaged → send to Twitch as usual
    ShadowSent,   // message was sent to shadow server
    ShadowFailed, // should have gone to shadow, but couldn't → do NOT send to Twitch
};

// ── Incoming message from server ────────────────────────────────────

struct ShadowMessage {
    QString id;   // 32 hex
    qint64 ts;    // server ms
    QString login;
    QString userId;
    QString displayName;
    bool mod = false;
    bool vip = false;
    QString text;
    QString parentId;  // non-empty = reply to this shadow message id
};

// ── gerr codes ──────────────────────────────────────────────────────

struct GerrInfo {
    QString code;
    double wait = 0.0;    // ratelimited: seconds to wait
    qint64 left = 0;      // banned: -1 = forever, >0 = seconds left
};

// ── gban / gunban ───────────────────────────────────────────────────

struct GbanInfo {
    QString roomId;
    QString userId;
    qint64 until = 0; // 0 = forever, else unix seconds
};

// ── gdel ────────────────────────────────────────────────────────────

struct GdelInfo {
    QString roomId;
    QString messageId;
};

// ── gstate ──────────────────────────────────────────────────────────

struct GstateInfo {
    QString roomId;
    bool enabled = true;
};

// ── ghist ───────────────────────────────────────────────────────────

struct GhostInfo {
    QString roomId;
    bool enabled = true;
    int banned = 0;
    bool mod = false;
    std::vector<ShadowMessage> msgs;
};

} // namespace chatterino::chatroom

namespace std {

template <>
struct hash<chatterino::chatroom::RoomSubscription> {
    size_t operator()(
        const chatterino::chatroom::RoomSubscription &s) const
    {
        return (size_t)qHash(s.roomId);
    }
};

} // namespace std

// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

#include <functional>
#include <optional>

class QObject;

namespace chatterino {

/**
 * Undocumented Twitch GraphQL (https://gql.twitch.tv/gql). Used where Helix
 * cannot satisfy the product need (e.g. viewing another channel's pinned
 * message as a plain viewer). May require Client-Integrity (JWT from POST
 * /integrity), can change without notice, and may have ToS implications.
 */
namespace TwitchGql {

struct PinnedChatMessage {
    QString id;
    QString sentAt;  // ISO timestamp from GQL
    QString senderDisplayName;
    QString senderLogin;
    QString senderId;
    QString senderChatColor;  // hex e.g. "#FF0000" when present
    QString text;
    /// Display name of the moderator/broadcaster who pinned the message
    /// (node.pinnedBy). Empty when the API doesn't return it.
    QString pinnedByDisplayName;
    /// Login of the pinner (for opening their profile).
    QString pinnedByLogin;
};

/// Persisted query GetPinnedChat: fetches the current moderator-pinned chat
/// message for @a channelId. Returns std::nullopt when nothing is pinned.
void fetchPinnedChatMessage(
    const QString &channelId, int count, const QString &oauthToken,
    const QString &gqlClientId, const QObject *caller,
    std::function<void(std::optional<PinnedChatMessage>)> onSuccess,
    std::function<void(QString)> onError);

}  // namespace TwitchGql

}  // namespace chatterino

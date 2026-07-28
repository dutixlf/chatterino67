// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

#include <memory>

namespace chatterino {

class TwitchAccount;

namespace chatroom {

struct TicketResult {
    QString ticket;
    qint64 exp = 0;     // unix seconds
    QString login;
    QString displayName;
    bool scoped = false;
};

/// Fetches and caches shadow-chat tickets from the upstream server.
/// One instance lives as long as the ChatroomManager.
class TicketClient : public QObject
{
    Q_OBJECT

public:
    /// Fetch a ticket for the given account.  If a cached ticket for the same
    /// login exists and is not within 10 minutes of expiry, it is returned
    /// without a network round-trip.
    void fetch(std::shared_ptr<TwitchAccount> account);

    /// Force-fetch even if the cached ticket is still fresh (used on
    /// verified:false / authstale).
    void forceFetch(std::shared_ptr<TwitchAccount> account);

    /// Clear cached ticket (used on account switch).
    void clearCache();

Q_SIGNALS:
    void ticketReady(const QString &ticket, const QString &login,
                     const QString &displayName, bool scoped);
    void ticketFailed(const QString &error);

private:
    void doFetch(std::shared_ptr<TwitchAccount> account);

    struct {
        QString ticket;
        qint64 exp = 0;
        QString login;
    } cache_;
};

} // namespace chatroom
} // namespace chatterino

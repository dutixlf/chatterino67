// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/chatroom/TicketClient.hpp"

#include "common/QLogging.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "providers/twitch/TwitchAccount.hpp"

#include <QStringBuilder>

namespace chatterino::chatroom {

static const QString TICKET_URL =
    QStringLiteral("https://ws.rish098.xyz/chatterino/typing/ticket");
static const qint64 RENEW_SECS_BEFORE_EXPIRY = 600; // 10 min

void TicketClient::fetch(std::shared_ptr<TwitchAccount> account)
{
    if (!account)
    {
        qCWarning(chatterinoChatroom) << "TicketClient: null account";
        Q_EMIT ticketFailed(QStringLiteral("not logged in"));
        return;
    }

    // Check cache
    if (!this->cache_.ticket.isEmpty() &&
        this->cache_.login == account->getUserName() &&
        this->cache_.exp > QDateTime::currentSecsSinceEpoch() + RENEW_SECS_BEFORE_EXPIRY)
    {
        qCDebug(chatterinoChatroom)
            << "TicketClient: using cached ticket, exp"
            << this->cache_.exp;
        // We need displayName — we don't cache it from the initial response
        // because the user doesn't change mid-session.  Just Q_EMIT what we have.
        Q_EMIT ticketReady(this->cache_.ticket, account->getUserName(),
                         account->getUserName(), true);
        return;
    }

    this->doFetch(account);
}

void TicketClient::forceFetch(std::shared_ptr<TwitchAccount> account)
{
    this->cache_ = {};
    if (account)
    {
        this->doFetch(account);
    }
}

void TicketClient::clearCache()
{
    this->cache_ = {};
}

void TicketClient::doFetch(std::shared_ptr<TwitchAccount> account)
{
    if (!account)
    {
        Q_EMIT ticketFailed(QStringLiteral("not logged in"));
        return;
    }

    QString token = account->getOAuthToken();
    if (token.isEmpty())
    {
        qCWarning(chatterinoChatroom) << "TicketClient: empty token";
        Q_EMIT ticketFailed(QStringLiteral("no oauth token"));
        return;
    }

    qCDebug(chatterinoChatroom) << "TicketClient: fetching ticket for"
                                << account->getUserName();

    NetworkRequest(TICKET_URL, NetworkRequestType::Get)
        .header("Authorization",
                QStringLiteral("Bearer ") % token)
        .timeout(10000)
        .onSuccess([this, account](const NetworkResult &res) {
            auto json = res.parseJson();
            QString ticket = json.value(QStringLiteral("ticket")).toString();
            qint64 exp =
                static_cast<qint64>(
                    json.value(QStringLiteral("exp")).toDouble());
            QString login =
                json.value(QStringLiteral("login")).toString();
            QString displayName =
                json.value(QStringLiteral("name")).toString();
            bool scoped =
                json.value(QStringLiteral("scoped")).toBool();

            if (ticket.isEmpty())
            {
                qCWarning(chatterinoChatroom)
                    << "TicketClient: server returned empty ticket";
                Q_EMIT ticketFailed(QStringLiteral("empty ticket from server"));
                return;
            }

            qCDebug(chatterinoChatroom)
                << "TicketClient: got ticket (exp" << exp
                << "login" << login << "scoped" << scoped << ")";

            this->cache_.ticket = ticket;
            this->cache_.exp = exp;
            this->cache_.login = login;

            Q_EMIT ticketReady(ticket, login, displayName, scoped);
        })
        .onError([this, account](const NetworkResult &res) {
            auto st = res.status();
            int status = st.value_or(0);
            auto body = res.getData();

            qCWarning(chatterinoChatroom)
                << "TicketClient: HTTP" << status << body;

            if (status == 401)
            {
                Q_EMIT ticketFailed(QStringLiteral("token rejected by server"));
            }
            else if (status == 429)
            {
                Q_EMIT ticketFailed(QStringLiteral("rate limited"));
            }
            else if (status >= 500)
            {
                Q_EMIT ticketFailed(QStringLiteral("server error"));
            }
            else
            {
                Q_EMIT ticketFailed(
                    QStringLiteral("HTTP ") % QString::number(status));
            }
        })
        .execute();
}

} // namespace chatterino::chatroom

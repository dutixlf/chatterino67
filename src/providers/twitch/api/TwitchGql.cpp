// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/twitch/api/TwitchGql.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QUrl>

#include <memory>

namespace {

using namespace chatterino;

// Twilight / twitch.tv web Client-ID. Helix-registered OAuth Client-IDs are
// often rejected with HTTP 400 on gql.twitch.tv; the Bearer token still
// identifies the user.
constexpr char TWITCH_WEB_GQL_CLIENT_ID[] = "kimne78kx3ncx6brgo4mv6wki5h1ko";

// Android TV app Client-ID (device-code OAuth); channel-points-miner uses this.
constexpr char TWITCH_TV_GQL_CLIENT_ID[] = "ue6666qo983tsx6so1t0vnawi233wa";

constexpr char TWITCH_GQL_CLIENT_VERSION[] =
    "ef928475-9403-42f2-8a34-55784bd08e16";

constexpr char TWITCH_TV_USER_AGENT[] =
    "Mozilla/5.0 (Linux; Android 7.1; Smart Box C1) AppleWebKit/537.36 (KHTML, "
    "like Gecko) Chrome/108.0.0.0 Safari/537.36";

QString twitchGqlStaticDeviceId()
{
    static QString id;
    if (!id.isEmpty())
    {
        return id;
    }
    id.resize(32);
    static constexpr char charset[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for (qsizetype i = 0; i < 32; ++i)
    {
        id[i] = QLatin1Char(charset[QRandomGenerator::global()->bounded(
            int(sizeof(charset) - 1))]);
    }
    return id;
}

QString twitchGqlStaticSessionId()
{
    static QString sid;
    if (!sid.isEmpty())
    {
        return sid;
    }
    QByteArray bytes(16, '\0');
    for (int i = 0; i < 16; ++i)
    {
        bytes[i] = char(QRandomGenerator::global()->bounded(256));
    }
    sid = QString::fromLatin1(bytes.toHex());
    return sid;
}

bool errorsSuggestIntegrity(const QJsonArray &errors)
{
    for (const auto &e : errors)
    {
        const auto msg =
            e.toObject().value(QStringLiteral("message")).toString().toLower();
        if (msg.contains(QStringLiteral("integrity")))
        {
            return true;
        }
    }
    return false;
}

QString summarizeGraphqlErrors(const QJsonArray &errors)
{
    QString out;
    for (const auto &e : errors)
    {
        if (!out.isEmpty())
        {
            out += QStringLiteral("; ");
        }
        out += e.toObject().value(QStringLiteral("message")).toString();
    }
    return out.isEmpty() ? QStringLiteral("GraphQL error") : out;
}

std::optional<TwitchGql::PinnedChatMessage> tryParsePinnedChat(
    const QJsonObject &root)
{
    const auto data = root.value(QStringLiteral("data")).toObject();
    const auto channel = data.value(QStringLiteral("channel")).toObject();
    if (channel.isEmpty())
    {
        return std::nullopt;
    }

    const auto pinnedConn =
        channel.value(QStringLiteral("pinnedChatMessages")).toObject();
    const auto edges = pinnedConn.value(QStringLiteral("edges")).toArray();
    if (edges.isEmpty())
    {
        return std::nullopt;
    }
    const auto node =
        edges.at(0).toObject().value(QStringLiteral("node")).toObject();
    if (node.isEmpty())
    {
        return std::nullopt;
    }
    // Who pinned it (node.pinnedBy), sibling of pinnedMessage. Defensive: this
    // stays empty if the persisted query doesn't return the field.
    const auto pinnedBy = node.value(QStringLiteral("pinnedBy")).toObject();
    const auto pinnedMessage =
        node.value(QStringLiteral("pinnedMessage")).toObject();
    if (pinnedMessage.isEmpty())
    {
        return std::nullopt;
    }

    TwitchGql::PinnedChatMessage out;
    out.id = pinnedMessage.value(QStringLiteral("id")).toString();
    out.sentAt = pinnedMessage.value(QStringLiteral("sentAt")).toString();
    out.text = pinnedMessage.value(QStringLiteral("content"))
                   .toObject()
                   .value(QStringLiteral("text"))
                   .toString();
    const auto sender =
        pinnedMessage.value(QStringLiteral("sender")).toObject();
    out.senderDisplayName =
        sender.value(QStringLiteral("displayName")).toString();
    out.senderLogin = sender.value(QStringLiteral("login")).toString();
    out.senderId = sender.value(QStringLiteral("id")).toString();
    out.senderChatColor = sender.value(QStringLiteral("chatColor")).toString();
    out.pinnedByLogin = pinnedBy.value(QStringLiteral("login")).toString();
    out.pinnedByDisplayName =
        pinnedBy.value(QStringLiteral("displayName")).toString();
    if (out.pinnedByDisplayName.isEmpty())
    {
        // Fallback to login if displayName isn't present.
        out.pinnedByDisplayName = out.pinnedByLogin;
    }
    if (out.id.isEmpty() || out.senderDisplayName.isEmpty())
    {
        return std::nullopt;
    }
    return out;
}

/// POST https://gql.twitch.tv/integrity — returns a Client-Integrity JWT.
void fetchIntegrityToken(const QString &oauthToken, const QObject *caller,
                         std::function<void(QString)> onDone)
{
    auto done =
        std::make_shared<std::function<void(QString)>>(std::move(onDone));
    QJsonObject emptyBody;
    NetworkRequest(QUrl(QStringLiteral("https://gql.twitch.tv/integrity")),
                   NetworkRequestType::Post)
        .timeout(10'000)
        .header("Client-ID", TWITCH_WEB_GQL_CLIENT_ID)
        .header("Authorization", QStringLiteral("Bearer %1").arg(oauthToken))
        .header("Content-Type", "application/json")
        .caller(caller)
        .json(emptyBody)
        .onSuccess([done](const NetworkResult &res) {
            const auto root = res.parseJson();
            QString token = root.value(QStringLiteral("token")).toString();
            (*done)(token);
        })
        .onError([done](const NetworkResult &) {
            (*done)({});
        })
        .execute();
}

/// Integrity token with TV-style headers (matches channel-points-miner).
void fetchIntegrityTokenTv(const QString &oauthToken, const QObject *caller,
                           std::function<void(QString)> onDone)
{
    auto done =
        std::make_shared<std::function<void(QString)>>(std::move(onDone));
    QJsonObject emptyBody;
    NetworkRequest(QUrl(QStringLiteral("https://gql.twitch.tv/integrity")),
                   NetworkRequestType::Post)
        .timeout(10'000)
        .header("Client-ID", TWITCH_TV_GQL_CLIENT_ID)
        .header("Authorization", QStringLiteral("OAuth %1").arg(oauthToken))
        .header("Client-Session-Id", twitchGqlStaticSessionId())
        .header("Client-Version", TWITCH_GQL_CLIENT_VERSION)
        .header("User-Agent", TWITCH_TV_USER_AGENT)
        .header("X-Device-Id", twitchGqlStaticDeviceId())
        .header("Content-Type", "application/json")
        .caller(caller)
        .json(emptyBody)
        .onSuccess([done](const NetworkResult &res) {
            const auto root = res.parseJson();
            QString token = root.value(QStringLiteral("token")).toString();
            (*done)(token);
        })
        .onError([done](const NetworkResult &) {
            (*done)({});
        })
        .execute();
}

}  // namespace

namespace chatterino {

namespace TwitchGql {

void fetchPinnedChatMessage(
    const QString &channelId, int count, const QString &oauthToken,
    const QString &gqlClientId, const QObject *caller,
    std::function<void(std::optional<PinnedChatMessage>)> onSuccess,
    std::function<void(QString)> onError)
{
    if (oauthToken.isEmpty())
    {
        onError(QStringLiteral("Missing OAuth token"));
        return;
    }
    const QString cid = channelId.trimmed();
    if (cid.isEmpty())
    {
        onError(QStringLiteral("Missing channel id"));
        return;
    }

    if (count <= 0)
    {
        count = 1;
    }

    const bool useTvGqlClient =
        gqlClientId.compare(QString::fromLatin1(TWITCH_TV_GQL_CLIENT_ID),
                            Qt::CaseInsensitive) == 0;

    auto runGql =
        std::make_shared<std::function<void(const QString &, bool)>>();

    *runGql = [runGql, cid, count, oauthToken, caller, onSuccess, onError,
               useTvGqlClient](const QString &integrityJwt,
                               bool afterIntegrityRetry) {
        QJsonObject variables;
        variables.insert(QStringLiteral("channelID"), cid);
        variables.insert(QStringLiteral("count"), count);

        QJsonObject persisted;
        persisted.insert(QStringLiteral("version"), 1);
        persisted.insert(QStringLiteral("sha256Hash"),
                         QStringLiteral("2d099d4c9b6af80a07d8440140c4f3dbb04d51"
                                        "6b35c401aab7ce8f60765308d5"));

        QJsonObject extensions;
        extensions.insert(QStringLiteral("persistedQuery"), persisted);

        QJsonObject body;
        body.insert(QStringLiteral("operationName"),
                    QStringLiteral("GetPinnedChat"));
        body.insert(QStringLiteral("variables"), variables);
        body.insert(QStringLiteral("extensions"), extensions);

        NetworkRequest req = [&] {
            if (useTvGqlClient)
            {
                return NetworkRequest(
                           QUrl(QStringLiteral("https://gql.twitch.tv/gql")),
                           NetworkRequestType::Post)
                    .timeout(10'000)
                    .header("Content-Type", "application/json")
                    .caller(caller)
                    .header("Client-ID", TWITCH_TV_GQL_CLIENT_ID)
                    .header("Authorization",
                            QStringLiteral("OAuth %1").arg(oauthToken))
                    .header("Client-Session-Id", twitchGqlStaticSessionId())
                    .header("Client-Version", TWITCH_GQL_CLIENT_VERSION)
                    .header("User-Agent", TWITCH_TV_USER_AGENT)
                    .header("X-Device-Id", twitchGqlStaticDeviceId());
            }
            return NetworkRequest(
                       QUrl(QStringLiteral("https://gql.twitch.tv/gql")),
                       NetworkRequestType::Post)
                .timeout(10'000)
                .header("Content-Type", "application/json")
                .caller(caller)
                .header("Client-ID", TWITCH_WEB_GQL_CLIENT_ID)
                .header("Authorization",
                        QStringLiteral("Bearer %1").arg(oauthToken));
        }();

        if (!integrityJwt.isEmpty())
        {
            req = std::move(req).header("Client-Integrity", integrityJwt);
        }

        std::move(req)
            .json(body)
            .onSuccess([runGql, afterIntegrityRetry, oauthToken, caller,
                        onError, onSuccess,
                        useTvGqlClient](const NetworkResult &res) {
                const auto root = res.parseJson();
                const auto errs =
                    root.value(QStringLiteral("errors")).toArray();

                if (!errs.isEmpty() && errorsSuggestIntegrity(errs) &&
                    !afterIntegrityRetry)
                {
                    if (useTvGqlClient)
                    {
                        fetchIntegrityTokenTv(oauthToken, caller,
                                              [runGql](QString jwt) {
                                                  (*runGql)(jwt, true);
                                              });
                    }
                    else
                    {
                        fetchIntegrityToken(oauthToken, caller,
                                            [runGql](QString jwt) {
                                                (*runGql)(jwt, true);
                                            });
                    }
                    return;
                }

                if (!errs.isEmpty() &&
                    (root.value(QStringLiteral("data")).isNull() ||
                     root.value(QStringLiteral("data")).toObject().isEmpty()))
                {
                    qCDebug(chatterinoTwitch)
                        << "Twitch GQL GetPinnedChat errors:"
                        << summarizeGraphqlErrors(errs);
                    onError(summarizeGraphqlErrors(errs));
                    return;
                }

                onSuccess(tryParsePinnedChat(root));
            })
            .onError([onError](const NetworkResult &res) {
                onError(res.formatError());
            })
            .execute();
    };

    if (useTvGqlClient)
    {
        (*runGql)(QString(), false);
    }
    else
    {
        fetchIntegrityToken(oauthToken, caller, [runGql](QString jwt) {
            (*runGql)(jwt, false);
        });
    }
}

}  // namespace TwitchGql

}  // namespace chatterino

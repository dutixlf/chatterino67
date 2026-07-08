// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/homies/HomiesBadges.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

namespace chatterino {

namespace {

QString fixUrl(const QString &url)
{
    if (url.startsWith("//"))
    {
        return "https:" + url;
    }
    return url;
}

}  // namespace

HomiesBadges::HomiesBadges()
{
    this->loadChatterinoHomies();
    this->loadDankChat();
    this->loadChatsen();
    this->loadChatty();
    this->loadPurpleTV();
    this->loadRTE();
}

std::vector<EmotePtr> HomiesBadges::getUserBadges(HomiesProvider provider,
                                                  const UserId &id,
                                                  const QString &username) const
{
    std::shared_lock lock(this->mutex_);

    std::vector<EmotePtr> result;
    const auto idx = providerIndex(provider);

    auto it = this->userBadges_[idx].find(id.string);
    if (it != this->userBadges_[idx].end())
    {
        result.insert(result.end(), it->second.begin(), it->second.end());
    }

    if (!username.isEmpty())
    {
        auto userIt = this->usernameBadges_[idx].find(username.toLower());
        if (userIt != this->usernameBadges_[idx].end())
        {
            result.insert(result.end(), userIt->second.begin(),
                          userIt->second.end());
        }
    }

    return result;
}

void HomiesBadges::addBadge(HomiesProvider provider, const QString &userId,
                            EmotePtr emote)
{
    std::unique_lock lock(this->mutex_);

    this->userBadges_[providerIndex(provider)][userId].push_back(
        std::move(emote));
}

void HomiesBadges::addBadgeByUsername(HomiesProvider provider,
                                      const QString &username, EmotePtr emote)
{
    std::unique_lock lock(this->mutex_);

    this->usernameBadges_[providerIndex(provider)][username.toLower()]
        .push_back(std::move(emote));
}

size_t HomiesBadges::providerIndex(HomiesProvider provider)
{
    return static_cast<size_t>(provider);
}

void HomiesBadges::loadChatterinoHomies()
{
    static QUrl url("https://chatterinohomies.com/api/badges/list");

    NetworkRequest(url)
        .concurrent()
        .onSuccess([this](auto result) {
            auto jsonRoot = result.parseJson();
            constexpr QSize baseSize(18, 18);

            for (const auto &badgeValue : jsonRoot.value("badges").toArray())
            {
                auto badge = badgeValue.toObject();
                auto tooltip = badge.value("tooltip").toString();
                auto emote = Emote{
                    .name = EmoteName{u"homies:" % tooltip},
                    .images =
                        ImageSet{
                            Image::fromUrl(
                                Url{badge.value("image1").toString()}, 1.0,
                                baseSize),
                            Image::fromUrl(
                                Url{badge.value("image2").toString()}, 0.5,
                                baseSize * 2),
                            Image::fromUrl(
                                Url{badge.value("image3").toString()}, 0.25,
                                baseSize * 4),
                        },
                    .tooltip = Tooltip{tooltip},
                    .homePage = Url{},
                };

                this->addBadge(HomiesProvider::ChatterinoHomies,
                               badge.value("userId").toString(),
                               std::make_shared<const Emote>(std::move(emote)));
            }
        })
        .execute();
}

void HomiesBadges::loadDankChat()
{
    static QUrl url("https://flxrs.com/api/badges");

    NetworkRequest(url)
        .concurrent()
        .onSuccess([this](auto result) {
            for (const auto &badgeValue : result.parseJsonArray())
            {
                auto badge = badgeValue.toObject();
                auto type = badge.value("type").toString();
                auto emote = Emote{
                    .name = EmoteName{u"dankchat:" % type},
                    .images = ImageSet(Image::fromAutoscaledUrl(
                        Url{badge.value("url").toString()}, 18)),
                    .tooltip = Tooltip{type},
                    .homePage = Url{},
                };

                auto emotePtr = std::make_shared<const Emote>(std::move(emote));
                for (const auto &user : badge.value("users").toArray())
                {
                    this->addBadge(HomiesProvider::DankChat, user.toString(),
                                   emotePtr);
                }
            }
        })
        .execute();
}

void HomiesBadges::loadChatsen()
{
    static QUrl url(
        "https://raw.githubusercontent.com/chatsen/resources/master/assets/"
        "data.json");

    NetworkRequest(url)
        .concurrent()
        .onSuccess([this](auto result) {
            auto jsonRoot = result.parseJson();

            QHash<QString, QJsonObject> badgeMeta;
            for (const auto &badgeValue : jsonRoot.value("badges").toArray())
            {
                auto badge = badgeValue.toObject();
                badgeMeta[badge.value("name").toString()] = badge;
            }

            for (const auto &userValue : jsonRoot.value("users").toArray())
            {
                auto user = userValue.toObject();
                auto userId = user.value("id").toString();
                for (const auto &userBadgeValue :
                     user.value("badges").toArray())
                {
                    auto userBadge = userBadgeValue.toObject();
                    auto badgeName = userBadge.value("badgeName").toString();
                    auto meta = badgeMeta.value(badgeName);
                    if (meta.isEmpty())
                    {
                        continue;
                    }

                    auto emote = Emote{
                        .name = EmoteName{u"chatsen:" % badgeName},
                        .images = ImageSet(Image::fromAutoscaledUrl(
                            Url{meta.value("image").toString()}, 18)),
                        .tooltip = Tooltip{meta.value("title").toString()},
                        .homePage = Url{},
                    };

                    this->addBadge(
                        HomiesProvider::Chatsen, userId,
                        std::make_shared<const Emote>(std::move(emote)));
                }
            }
        })
        .execute();

    static QUrl patreonUrl("https://api.chatsen.app/account/badges");

    NetworkRequest(patreonUrl)
        .concurrent()
        .onSuccess([this](auto result) {
            static const QHash<QString, QString> TIER_MAP = {
                {"Chatsen Patreon: Tier 1", "patreon_tier1"},
                {"Chatsen Patreon: Tier 2", "patreon_tier2"},
                {"Chatsen Patreon: Tier 3", "patreon_tier3"},
            };

            for (const auto &badgeValue : result.parseJsonArray())
            {
                auto badge = badgeValue.toObject();
                auto badgeName = TIER_MAP.value(badge.value("name").toString());
                if (badgeName.isEmpty())
                {
                    continue;
                }

                auto mipmap = badge.value("mipmap").toArray();
                if (mipmap.isEmpty())
                {
                    continue;
                }

                auto emote = Emote{
                    .name = EmoteName{u"chatsen:" % badgeName},
                    .images = ImageSet(Image::fromAutoscaledUrl(
                        Url{mipmap.first().toString()}, 18)),
                    .tooltip = Tooltip{badge.value("name").toString()},
                    .homePage = Url{},
                };

                auto emotePtr = std::make_shared<const Emote>(std::move(emote));
                for (const auto &user : badge.value("users").toArray())
                {
                    this->addBadge(HomiesProvider::Chatsen,
                                   user.toVariant().toString(), emotePtr);
                }
            }
        })
        .execute();
}

void HomiesBadges::loadChatty()
{
    static QUrl url("https://tduva.com/res/badges");

    NetworkRequest(url)
        .concurrent()
        .onSuccess([this](auto result) {
            for (const auto &badgeValue : result.parseJsonArray())
            {
                auto badge = badgeValue.toObject();
                if (badge.value("id").toString() != "chatty")
                {
                    continue;
                }

                auto title = badge.value("meta_title").toString();
                auto emote = Emote{
                    .name = EmoteName{u"chatty:" % title},
                    .images =
                        ImageSet{
                            Image::fromUrl(Url{
                                fixUrl(badge.value("image_url").toString())}),
                            Image::fromUrl(
                                Url{fixUrl(
                                    badge.value("image_url_2").toString())},
                                0.5),
                            Image::fromUrl(
                                Url{fixUrl(
                                    badge.value("image_url_4").toString())},
                                0.25),
                        },
                    .tooltip = Tooltip{title},
                    .homePage = Url{fixUrl(badge.value("meta_url").toString())},
                };

                auto emotePtr = std::make_shared<const Emote>(std::move(emote));
                for (const auto &user : badge.value("usernames").toArray())
                {
                    this->addBadgeByUsername(HomiesProvider::Chatty,
                                             user.toString(), emotePtr);
                }
            }
        })
        .execute();
}

void HomiesBadges::loadPurpleTV()
{
    static QUrl url("https://api.nopbreak.ru/orange/donations");

    NetworkRequest(url)
        .concurrent()
        .onSuccess([this](auto result) {
            auto jsonRoot = result.parseJson();
            auto defaultUrl = jsonRoot.value("defaultBadgeUrl").toString();

            for (const auto &userValue : jsonRoot.value("users").toArray())
            {
                auto user = userValue.toObject();
                auto badgeUrl = user.value("badgeUrl").toString();
                if (badgeUrl.isEmpty())
                {
                    badgeUrl = defaultUrl;
                }
                if (badgeUrl.isEmpty())
                {
                    continue;
                }

                auto emote = Emote{
                    .name = EmoteName{u"purpletv:" %
                                      user.value("userName").toString()},
                    .images =
                        ImageSet(Image::fromAutoscaledUrl(Url{badgeUrl}, 18)),
                    .tooltip = Tooltip{"PurpleTV"},
                    .homePage = Url{},
                };

                this->addBadge(HomiesProvider::PurpleTV,
                               user.value("userId").toString(),
                               std::make_shared<const Emote>(std::move(emote)));
            }
        })
        .execute();
}

void HomiesBadges::loadRTE()
{
    static QUrl url("https://ext.rte.net.ru:8443/api/badge-users");

    NetworkRequest(url)
        .concurrent()
        .onSuccess([this](auto result) {
            for (const auto &entryValue : result.parseJsonArray())
            {
                auto entry = entryValue.toObject();
                auto badgeUrl = entry.value("badgeUrl").toString();
                if (badgeUrl.isEmpty())
                {
                    continue;
                }

                auto userId = entry.value("userId").toString();
                auto emote = Emote{
                    .name = EmoteName{u"rte:" % userId},
                    .images =
                        ImageSet(Image::fromAutoscaledUrl(Url{badgeUrl}, 18)),
                    .tooltip = Tooltip{"ReYohoho"},
                    .homePage = Url{},
                };

                this->addBadge(HomiesProvider::RTE, userId,
                               std::make_shared<const Emote>(std::move(emote)));
            }
        })
        .execute();
}

}  // namespace chatterino

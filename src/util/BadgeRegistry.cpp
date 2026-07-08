// SPDX-FileCopyrightText: 2022 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "util/BadgeRegistry.hpp"

#include "common/QLogging.hpp"
#include "messages/Emote.hpp"
#include "providers/seventv/eventapi/Dispatch.hpp"
#include "util/Helpers.hpp"
#include "util/Variant.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

namespace {

void clearIfEquals(auto &map, auto &&key, const auto &expectedID)
{
    const auto it = map.find(std::forward<decltype(key)>(key));
    if (it != map.end() && it->second->id.string == expectedID)
    {
        map.erase(it);
    }
}

}  // namespace

namespace chatterino {

std::optional<EmotePtr> BadgeRegistry::getBadge(const UserId &id) const
{
    std::shared_lock lock(this->mutex_);

    auto it = this->badgeMap_.find(id.string);
    if (it != this->badgeMap_.end())
    {
        return it->second;
    }
    return std::nullopt;
}

std::optional<EmotePtr> BadgeRegistry::getKickBadge(uint64_t id) const
{
    std::shared_lock lock(this->mutex_);

    auto it = this->kickBadgeMap_.find(id);
    if (it != this->kickBadgeMap_.end())
    {
        return it->second;
    }
    return std::nullopt;
}

void BadgeRegistry::assignBadgeToUser(const QString &badgeID,
                                      const UserId &userID)
{
    const std::unique_lock lock(this->mutex_);

    const auto badgeIt = this->knownBadges_.find(badgeID);
    if (badgeIt != this->knownBadges_.end())
    {
        this->badgeMap_[userID.string] = badgeIt->second;
    }
}

void BadgeRegistry::assignBadgeToUsers(
    const QString &badgeID, std::span<const seventv::eventapi::User> users)
{
    const std::unique_lock lock(this->mutex_);

    const auto badgeIt = this->knownBadges_.find(badgeID);
    if (badgeIt == this->knownBadges_.end())
    {
        return;
    }
    for (const auto &user : users)
    {
        std::visit(
            variant::Overloaded{
                [&](const seventv::eventapi::TwitchUser &u) {
                    this->badgeMap_[u.id] = badgeIt->second;
                    rawBadgeAssignmentsCache_.append({u.id, badgeID, false});
                },
                [&](const seventv::eventapi::KickUser &u) {
                    this->kickBadgeMap_[u.id] = badgeIt->second;
                    rawBadgeAssignmentsCache_.append(
                        {QString::number(u.id), badgeID, true});
                },
            },
            user);
    }
}

void BadgeRegistry::clearBadgeFromUser(const QString &badgeID,
                                       const UserId &userID)
{
    const std::unique_lock lock(this->mutex_);

    clearIfEquals(this->badgeMap_, userID.string, badgeID);
}

void BadgeRegistry::clearBadgeFromUsers(
    const QString &badgeID, std::span<const seventv::eventapi::User> users)
{
    const std::unique_lock lock(this->mutex_);

    for (const auto &user : users)
    {
        std::visit(variant::Overloaded{
                       [&](const seventv::eventapi::TwitchUser &u) {
                           clearIfEquals(this->badgeMap_, u.id, badgeID);
                       },
                       [&](const seventv::eventapi::KickUser &u) {
                           clearIfEquals(this->kickBadgeMap_, u.id, badgeID);
                       },
                   },
                   user);
    }
}

QString BadgeRegistry::registerBadge(const QJsonObject &badgeJson)
{
    const auto badgeID = this->idForBadge(badgeJson);

    const std::unique_lock lock(this->mutex_);

    if (this->knownBadges_.contains(badgeID))
    {
        return badgeID;
    }

    auto emote = this->createBadge(badgeID, badgeJson);
    if (!emote)
    {
        return badgeID;
    }

    this->knownBadges_[badgeID] = std::move(emote);
    this->rawBadgesCache_[badgeID] = badgeJson;
    return badgeID;
}

void BadgeRegistry::saveCache() const
{
    std::shared_lock lock(this->mutex_);
    this->serializeCache();
}

void BadgeRegistry::serializeCache() const
{
    QJsonObject root;
    QJsonArray badgesArray;
    for (auto it = this->rawBadgesCache_.constBegin();
         it != this->rawBadgesCache_.constEnd(); ++it)
    {
        badgesArray.append(it.value());
    }
    root["badges"] = badgesArray;

    QJsonArray assignmentsArray;
    for (const auto &[userId, badgeID, isKick] :
         this->rawBadgeAssignmentsCache_)
    {
        QJsonObject entry;
        entry["userId"] = userId;
        entry["badgeID"] = badgeID;
        entry["kick"] = isKick;
        assignmentsArray.append(entry);
    }
    root["assignments"] = assignmentsArray;

    writeProviderEmotesCache(
        cacheProviderName(), "cache",
        QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void BadgeRegistry::loadCache()
{
    readProviderEmotesCache(
        cacheProviderName(), "cache", [this](const QJsonDocument &doc) {
            auto root = doc.object();
            auto badges = root["badges"].toArray();
            for (const auto &badgeVal : badges)
            {
                this->registerBadge(badgeVal.toObject());
            }

            auto assignments = root["assignments"].toArray();
            for (const auto &assignVal : assignments)
            {
                auto entry = assignVal.toObject();
                auto userId = entry["userId"].toString();
                auto badgeID = entry["badgeID"].toString();
                bool isKick = entry["kick"].toBool();

                std::unique_lock lock(this->mutex_);
                const auto badgeIt = this->knownBadges_.find(badgeID);
                if (badgeIt != this->knownBadges_.end())
                {
                    if (isKick)
                    {
                        this->kickBadgeMap_[userId.toULongLong()] =
                            badgeIt->second;
                    }
                    else
                    {
                        this->badgeMap_[userId] = badgeIt->second;
                    }
                }
            }
        });
}

}  // namespace chatterino

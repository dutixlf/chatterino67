// SPDX-FileCopyrightText: 2023 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/seventv/SeventvAPI.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "controllers/userdata/UserDataController.hpp"
#include "common/Literals.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "util/Helpers.hpp"

namespace {

using namespace chatterino::literals;

const QString API_URL_USER = u"https://7tv.io/v3/users/twitch/%1"_s;
const QString API_URL_KICK_USER = u"https://7tv.io/v3/users/kick/%1"_s;
const QString API_URL_EMOTE_SET = u"https://7tv.io/v3/emote-sets/%1"_s;
const QString API_URL_PRESENCES = u"https://7tv.io/v3/users/%1/presences"_s;

}  // namespace

// NOLINTBEGIN(readability-convert-member-functions-to-static)
namespace chatterino {

void SeventvAPI::getUserByTwitchID(
    const QString &twitchID, SuccessCallback<const QJsonObject &> &&onSuccess,
    ErrorCallback &&onError)
{
    NetworkRequest(proxiedUrl(API_URL_USER.arg(twitchID)), NetworkRequestType::Get)
        .timeout(20000)
        .onSuccess(
            [callback = std::move(onSuccess)](const NetworkResult &result) {
                auto json = result.parseJson();
                callback(json);
            })
        .onError([callback = std::move(onError)](const NetworkResult &result) {
            callback(result);
        })
        .execute();
}

void SeventvAPI::getUserByKickID(
    uint64_t userID, SuccessCallback<const QJsonObject &> &&onSuccess,
    ErrorCallback &&onError)
{
    NetworkRequest(proxiedUrl(API_URL_KICK_USER.arg(userID)), NetworkRequestType::Get)
        .timeout(20000)
        .onSuccess(
            [callback = std::move(onSuccess)](const NetworkResult &result) {
                auto json = result.parseJson();
                callback(json);
            })
        .onError([callback = std::move(onError)](const NetworkResult &result) {
            callback(result);
        })
        .execute();
}

void SeventvAPI::getEmoteSet(const QString &emoteSet,
                             SuccessCallback<const QJsonObject &> &&onSuccess,
                             ErrorCallback &&onError)
{
    NetworkRequest(proxiedUrl(API_URL_EMOTE_SET.arg(emoteSet)), NetworkRequestType::Get)
        .timeout(25000)
        .onSuccess(
            [callback = std::move(onSuccess)](const NetworkResult &result) {
                auto json = result.parseJson();
                callback(json);
            })
        .onError([callback = std::move(onError)](const NetworkResult &result) {
            callback(result);
        })
        .execute();
}

void SeventvAPI::updateTwitchPresence(const QString &twitchChannelID,
                                      const QString &seventvUserID,
                                      SuccessCallback<> &&onSuccess,
                                      ErrorCallback &&onError)
{
    this->updatePresence(u"TWITCH"_s, twitchChannelID, seventvUserID,
                         std::move(onSuccess), std::move(onError));
}

void SeventvAPI::updateKickPresence(uint64_t kickUserID,
                                    const QString &seventvUserID,
                                    SuccessCallback<> &&onSuccess,
                                    ErrorCallback &&onError)
{
    this->updatePresence(u"KICK"_s, QString::number(kickUserID), seventvUserID,
                         std::move(onSuccess), std::move(onError));
}

void SeventvAPI::updatePresence(const QString &platform,
                                const QString &platformID,
                                const QString &seventvUserID,
                                SuccessCallback<> &&onSuccess,
                                ErrorCallback &&onError)
{
    QJsonObject payload{
        {u"kind"_s, 1},  // UserPresenceKindChannel
        {u"data"_s,
         QJsonObject{
             {u"id"_s, platformID},
             {u"platform"_s, platform},
         }},
    };

    NetworkRequest(proxiedUrl(API_URL_PRESENCES.arg(seventvUserID)),
                   NetworkRequestType::Post)
        .json(payload)
        .timeout(10000)
        .onSuccess([callback = std::move(onSuccess)](const auto &) {
            callback();
        })
        .onError([callback = std::move(onError)](const NetworkResult &result) {
            callback(result);
        })
        .execute();
}

}  // namespace chatterino
// NOLINTEND(readability-convert-member-functions-to-static)

namespace chatterino {

std::optional<QColor> SeventvAPI::getCachedUserColor(const QString &twitchID) const
{
    std::lock_guard lock(this->colorCacheMutex_);
    auto it = this->userColorCache_.constFind(twitchID);
    if (it != this->userColorCache_.constEnd() && it->isValid())
    {
        return *it;
    }
    return std::nullopt;
}

void SeventvAPI::fetchUserColor(const QString &twitchID)
{
    // Check cache first to avoid duplicate requests
    {
        std::lock_guard lock(this->colorCacheMutex_);
        if (this->userColorCache_.contains(twitchID))
        {
            return;
        }
        // Mark as pending with invalid color to deduplicate requests
        this->userColorCache_.insert(twitchID, QColor{});
    }

    this->getUserByTwitchID(
        twitchID,
        [this, twitchID](const QJsonObject &json) {
            // 7TV v3: color lives at user.style.color as a 0xRRGGBBAA int
            const auto user = json["user"].toObject();
            const auto style = user["style"].toObject();
            const auto colorInt =
                static_cast<uint32_t>(style["color"].toVariant().toLongLong());
            if (colorInt != 0)
            {
                QColor color(static_cast<int>((colorInt >> 24) & 0xFF),
                             static_cast<int>((colorInt >> 16) & 0xFF),
                             static_cast<int>((colorInt >> 8) & 0xFF),
                             static_cast<int>(colorInt & 0xFF));
                if (color.isValid())
                {
                    {
                        std::lock_guard lock(this->colorCacheMutex_);
                        this->userColorCache_.insert(twitchID, color);
                    }
                    // Also store in UserData DB for persistence
                    if (auto *userData = getApp()->getUserData())
                    {
                        userData->setUserColor(twitchID, color.name(QColor::HexArgb));
                    }
                    qCDebug(chatterinoSeventv)
                        << "7TV color for" << twitchID << "="
                        << color.name(QColor::HexArgb);
                    return;
                }
            }
            // No color in response — drop pending marker so we retry later
            std::lock_guard lock(this->colorCacheMutex_);
            this->userColorCache_.remove(twitchID);
            qCDebug(chatterinoSeventv)
                << "No 7TV color for" << twitchID;
        },
        [this, twitchID](const NetworkResult &) {
            // Remove pending marker on failure
            std::lock_guard lock(this->colorCacheMutex_);
            this->userColorCache_.remove(twitchID);
            qCDebug(chatterinoSeventv)
                << "7TV color fetch failed for" << twitchID;
        });
}

}  // namespace chatterino

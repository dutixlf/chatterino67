// SPDX-FileCopyrightText: 2023 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <functional>
#include <optional>

class QString;
class QJsonObject;
class QColor;

namespace chatterino {

class NetworkResult;

class SeventvAPI final
{
    using ErrorCallback = std::function<void(const NetworkResult &)>;
    template <typename... T>
    using SuccessCallback = std::function<void(T...)>;

public:
    SeventvAPI() = default;
    ~SeventvAPI() = default;

    SeventvAPI(const SeventvAPI &) = delete;
    SeventvAPI(SeventvAPI &&) = delete;
    SeventvAPI &operator=(const SeventvAPI &) = delete;
    SeventvAPI &operator=(SeventvAPI &&) = delete;

    void getUserByTwitchID(const QString &twitchID,
                           SuccessCallback<const QJsonObject &> &&onSuccess,
                           ErrorCallback &&onError);
    void getUserByKickID(uint64_t userID,
                         SuccessCallback<const QJsonObject &> &&onSuccess,
                         ErrorCallback &&onError);
    void getEmoteSet(const QString &emoteSet,
                     SuccessCallback<const QJsonObject &> &&onSuccess,
                     ErrorCallback &&onError);

    void updateTwitchPresence(const QString &twitchChannelID,
                              const QString &seventvUserID,
                              SuccessCallback<> &&onSuccess,
                              ErrorCallback &&onError);

    void updateKickPresence(uint64_t kickUserID, const QString &seventvUserID,
                            SuccessCallback<> &&onSuccess,
                            ErrorCallback &&onError);

    void updatePresence(const QString &platform, const QString &platformID,
                        const QString &seventvUserID,
                        SuccessCallback<> &&onSuccess,
                        ErrorCallback &&onError);

    // Color cache from 7TV REST API
    std::optional<QColor> getCachedUserColor(const QString &twitchID) const;
    void fetchUserColor(const QString &twitchID);

private:
    mutable std::mutex colorCacheMutex_;
    QHash<QString, QColor> userColorCache_;
};

}  // namespace chatterino

#include "providers/seventv/SeventvPaints.hpp"

#include "Application.hpp"
#include "common/QLogging.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "messages/Image.hpp"
#include "providers/seventv/eventapi/Dispatch.hpp"
#include "providers/seventv/paints/LinearGradientPaint.hpp"
#include "providers/seventv/paints/PaintDropShadow.hpp"
#include "providers/seventv/paints/RadialGradientPaint.hpp"
#include "providers/seventv/paints/UrlPaint.hpp"
#include "singletons/Settings.hpp"
#include "singletons/WindowManager.hpp"
#include "util/DebugCount.hpp"
#include "util/Helpers.hpp"
#include "util/PostToThread.hpp"
#include "util/Variant.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

namespace {
using namespace chatterino;
using namespace Qt::Literals;

QColor rgbaToQColor(const uint32_t color)
{
    auto red = (int)((color >> 24) & 0xFF);
    auto green = (int)((color >> 16) & 0xFF);
    auto blue = (int)((color >> 8) & 0xFF);
    auto alpha = (int)(color & 0xFF);

    return {red, green, blue, alpha};
}

std::optional<QColor> parsePaintColor(const QJsonValue &color)
{
    if (color.isNull())
    {
        return std::nullopt;
    }

    return rgbaToQColor(color.toInt());
}

QGradientStops parsePaintStops(const QJsonArray &stops)
{
    QGradientStops parsedStops;
    double lastStop = -1;

    for (const auto &stop : stops)
    {
        const auto stopObject = stop.toObject();

        const auto rgbaColor = stopObject["color"].toInt();
        auto position = stopObject["at"].toDouble();

        // HACK: qt does not support hard edges in gradients like css does
        // Setting a different color at the same position twice just overwrites
        // the previous color. So we have to shift the second point slightly
        // ahead, simulating an actual hard edge
        if (position <= lastStop)
        {
            position = lastStop + 0.0000001;
        }

        lastStop = position;
        parsedStops.append(QGradientStop(position, rgbaToQColor(rgbaColor)));
    }

    return parsedStops;
}

std::vector<PaintDropShadow> parseDropShadows(const QJsonArray &dropShadows)
{
    std::vector<PaintDropShadow> parsedDropShadows;

    for (const auto &shadow : dropShadows)
    {
        const auto shadowObject = shadow.toObject();

        const auto xOffset = shadowObject["x_offset"].toDouble();
        const auto yOffset = shadowObject["y_offset"].toDouble();
        const auto radius = shadowObject["radius"].toDouble();
        const auto rgbaColor = shadowObject["color"].toInt();

        parsedDropShadows.emplace_back(xOffset, yOffset, radius,
                                       rgbaToQColor(rgbaColor));
    }

    return parsedDropShadows;
}

std::optional<std::shared_ptr<Paint>> parsePaint(const QJsonObject &paintJson)
{
    const QString name = paintJson["name"].toString();
    const QString id = paintJson["id"].toString();

    const auto color = parsePaintColor(paintJson["color"]);
    const bool repeat = paintJson["repeat"].toBool();
    const float angle = (float)paintJson["angle"].toDouble();

    const QGradientStops stops = parsePaintStops(paintJson["stops"].toArray());

    const auto shadows = parseDropShadows(paintJson["shadows"].toArray());

    const QString function = paintJson["function"].toString();
    if (function == "LINEAR_GRADIENT" || function == "linear-gradient")
    {
        return std::make_shared<LinearGradientPaint>(name, id, color, stops,
                                                     repeat, angle, shadows);
    }

    if (function == "RADIAL_GRADIENT" || function == "radial-gradient")
    {
        return std::make_shared<RadialGradientPaint>(name, id, stops, repeat,
                                                     shadows);
    }

    if (function == "URL" || function == "url")
    {
        const QString url = paintJson["image_url"].toString();
        const ImagePtr image = Image::fromUrl({url}, 1);
        if (image == nullptr)
        {
            return std::nullopt;
        }

        return std::make_shared<UrlPaint>(name, id, image, shadows);
    }

    return std::nullopt;
}

}  // namespace

namespace chatterino {

SeventvPaints::SeventvPaints() = default;

void SeventvPaints::loadRTEPaints()
{
    // ponytail: fetch paints from ReYohoho API (same format as 7TV)
    // then fetch user-paints after paints are loaded to avoid race
    NetworkRequest(QUrl("https://ext.rte.net.ru:8443/api/paints"))
        .concurrent()
        .onSuccess([this](NetworkResult result) {
            auto root = result.parseJson();
            auto paints = root["paints"].toArray();
            for (const auto &paintVal : paints)
            {
                this->addPaint(paintVal.toObject(), true);
            }
            qCDebug(chatterinoSeventv)
                << "Loaded" << paints.size() << "RTE paints";

            // now fetch user-paints assignments (paints are in knownPaints_)
            NetworkRequest(
                QUrl("https://ext.rte.net.ru:8443/api/user-paints"))
                .concurrent()
                .onSuccess([this](NetworkResult result2) {
                    auto users = result2.parseJsonArray();
                    std::unique_lock lock(this->mutex_);
                    int nAssigned = 0;
                    for (const auto &userVal : users)
                    {
                        auto entry = userVal.toObject();
                        auto twitchId = entry["twitchId"].toString();
                        auto paintID = entry["paintId"].toString();
                        auto it = this->knownPaints_.find(paintID);
                        if (it != this->knownPaints_.end())
                        {
                            this->twitchPaintMap_[twitchId] = it->second;
                            nAssigned++;
                        }
                    }
                    qCDebug(chatterinoSeventv)
                        << "Assigned" << nAssigned << "RTE user paints";
                    if (nAssigned > 0)
                    {
                        postToThread([] {
                            getApp()->getWindows()
                                ->invalidateChannelViewBuffers();
                        });
                    }
                })
                .execute();
        })
        .execute();
}

std::shared_ptr<Paint> SeventvPaints::getPaint(const QString &userName,
                                               bool kick) const
{
    std::shared_lock lock(this->mutex_);

    auto check = [&](const auto &map) -> std::shared_ptr<Paint> {
        const auto it = map.find(userName);
        if (it == map.end())
        {
            return nullptr;
        }
        // ponytail: filter by source setting
        if (it->second->isRTE && !getSettings()->displayRTEPaints)
        {
            return nullptr;
        }
        if (!it->second->isRTE && !getSettings()->displaySevenTVPaints)
        {
            return nullptr;
        }
        return it->second;
    };

    if (kick)
    {
        return check(this->kickPaintMap_);
    }
    return check(this->twitchPaintMap_);
}

void SeventvPaints::addPaint(const QJsonObject &paintJson, bool isRTE)
{
    const auto paintID = paintJson["id"].toString();

    std::unique_lock lock(this->mutex_);

    if (this->knownPaints_.contains(paintID))
    {
        return;
    }

    std::optional<std::shared_ptr<Paint>> paint = parsePaint(paintJson);
    if (!paint)
    {
        return;
    }

    (*paint)->isRTE = isRTE;
    DebugCount::increase(DebugObject::SeventvPaints);
    this->knownPaints_[paintID] = *paint;
    this->rawPaintsCache_[paintID] = paintJson;
}

void SeventvPaints::assignPaintToUsers(
    const QString &paintID, std::span<const seventv::eventapi::User> users)
{
    std::unique_lock lock(this->mutex_);

    const auto paintIt = this->knownPaints_.find(paintID);
    if (paintIt == this->knownPaints_.end())
    {
        return;
    }

    bool changed = false;
    int64_t nAdded = 0;
    auto addToMap = [&](auto &map, const QString &username) {
        auto it = map.find(username);
        if (it == map.end())
        {
            map.emplace(username, paintIt->second);
            changed = true;
            nAdded++;
        }
        else if (it->second != paintIt->second)
        {
            it->second = paintIt->second;
            changed = true;
        }
    };
    for (const auto &user : users)
    {
        std::visit(
            variant::Overloaded{
                [&](const seventv::eventapi::TwitchUser &u) {
                    addToMap(this->twitchPaintMap_, u.userName);
                    rawAssignmentsCache_.append({u.userName, paintID, false});
                },
                [&](const seventv::eventapi::KickUser &u) {
                    addToMap(this->kickPaintMap_, u.userName);
                    rawAssignmentsCache_.append({u.userName, paintID, true});
                },
            },
            user);
    }

    if (nAdded > 0)
    {
        DebugCount::increase(DebugObject::SeventvPaintAssignments, nAdded);
    }

    if (changed)
    {
        postToThread([] {
            getApp()->getWindows()->invalidateChannelViewBuffers();
        });
    }
}

void SeventvPaints::clearPaintFromUsers(
    const QString &paintID, std::span<const seventv::eventapi::User> users)
{
    std::unique_lock lock(this->mutex_);

    int64_t nRemoved = 0;
    auto removeFromMap = [&](auto &map, const QString &username) {
        const auto it = map.find(username);
        if (it != map.end() && it->second->id == paintID)
        {
            map.erase(it);
            nRemoved++;
        }
    };
    for (const auto &user : users)
    {
        std::visit(variant::Overloaded{
                       [&](const seventv::eventapi::TwitchUser &u) {
                           removeFromMap(this->twitchPaintMap_, u.userName);
                       },
                       [&](const seventv::eventapi::KickUser &u) {
                           removeFromMap(this->kickPaintMap_, u.userName);
                       },
                   },
                   user);
    }

    if (nRemoved > 0)
    {
        DebugCount::decrease(DebugObject::SeventvPaintAssignments, nRemoved);
        postToThread([] {
            getApp()->getWindows()->invalidateChannelViewBuffers();
        });
    }
}

void SeventvPaints::saveCache() const
{
    std::shared_lock lock(this->mutex_);
    this->serializeCache();
}

void SeventvPaints::serializeCache() const
{
    QJsonObject root;
    QJsonArray paintsArray;
    for (auto it = this->rawPaintsCache_.constBegin();
         it != this->rawPaintsCache_.constEnd(); ++it)
    {
        paintsArray.append(it.value());
    }
    root["paints"] = paintsArray;

    QJsonArray assignmentsArray;
    for (const auto &[username, paintID, isKick] : this->rawAssignmentsCache_)
    {
        QJsonObject entry;
        entry["username"] = username;
        entry["paintID"] = paintID;
        entry["kick"] = isKick;
        assignmentsArray.append(entry);
    }
    root["assignments"] = assignmentsArray;

    writeProviderEmotesCache(
        "seventv-paints", "cache",
        QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void SeventvPaints::loadCache()
{
    readProviderEmotesCache(
        "seventv-paints", "cache", [this](const QJsonDocument &doc) {
            auto root = doc.object();
            auto paints = root["paints"].toArray();
            for (const auto &paintVal : paints)
            {
                this->addPaint(paintVal.toObject());
            }

            auto assignments = root["assignments"].toArray();
            for (const auto &assignVal : assignments)
            {
                auto entry = assignVal.toObject();
                auto username = entry["username"].toString();
                auto paintID = entry["paintID"].toString();
                bool isKick = entry["kick"].toBool();

                std::unique_lock lock(this->mutex_);
                const auto paintIt = this->knownPaints_.find(paintID);
                if (paintIt != this->knownPaints_.end())
                {
                    if (isKick)
                    {
                        this->kickPaintMap_[username] = paintIt->second;
                    }
                    else
                    {
                        this->twitchPaintMap_[username] = paintIt->second;
                    }
                }
            }

            if (!paints.isEmpty() || !assignments.isEmpty())
            {
                qCDebug(chatterinoCache)
                    << "Loaded 7TV paints cache:" << paints.size() << "paints,"
                    << assignments.size() << "assignments";
            }
        });
}

}  // namespace chatterino

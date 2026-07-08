// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Aliases.hpp"

#include <array>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace chatterino {

struct Emote;
using EmotePtr = std::shared_ptr<const Emote>;

enum class HomiesProvider {
    ChatterinoHomies,
    DankChat,
    Chatsen,
    Chatty,
    PurpleTV,
    RTE,
};

class HomiesBadges
{
public:
    HomiesBadges();

    std::vector<EmotePtr> getUserBadges(HomiesProvider provider,
                                        const UserId &id,
                                        const QString &username = {}) const;

private:
    void addBadge(HomiesProvider provider, const QString &userId,
                  EmotePtr emote);
    void addBadgeByUsername(HomiesProvider provider, const QString &username,
                            EmotePtr emote);

    static size_t providerIndex(HomiesProvider provider);

    void loadChatterinoHomies();
    void loadDankChat();
    void loadChatsen();
    void loadChatty();
    void loadPurpleTV();
    void loadRTE();

    mutable std::shared_mutex mutex_;

    std::array<std::unordered_map<QString, std::vector<EmotePtr>>, 6>
        userBadges_;
    std::array<std::unordered_map<QString, std::vector<EmotePtr>>, 6>
        usernameBadges_;
};

}  // namespace chatterino

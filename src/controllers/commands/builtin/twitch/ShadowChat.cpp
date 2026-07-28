// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "Application.hpp"
#include "common/Channel.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "controllers/commands/builtin/twitch/ShadowChat.hpp"
#include "singletons/Settings.hpp"

namespace chatterino::commands {

QString toggleShadowChat(const CommandContext &ctx)
{
    if (!ctx.channel)
    {
        return {};
    }

    auto &settings = *getSettings();
    settings.shadowChatEnabled = true;
    bool newVal = !settings.shadowChatSendToShadow;
    settings.shadowChatSendToShadow = newVal;

    ctx.channel->addSystemMessage(
        newVal ? "Shadow chat enabled. Messages will be sent to shadow chat."
               : "Shadow chat disabled. Messages will be sent normally.");

    return {};
}

}  // namespace chatterino::commands

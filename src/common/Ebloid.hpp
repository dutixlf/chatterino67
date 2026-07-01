// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

#include <cstdint>
#include <optional>
#include <string_view>

namespace chatterino {

/// @brief Streamer-mode behaviors for inline eblo.id images.
enum class EbloidStreamerMode : std::uint8_t {
    /// Images are loaded and shown normally.
    Disabled,
    /// Images are loaded but shown blurred; clicking unblurs the image.
    Blur,
    /// Images are not loaded until the link is clicked; Ctrl+click opens the
    /// eblo.id page in a browser.
    ClickToLoad,
};

constexpr std::optional<std::string_view> qmagicenumDisplayName(
    EbloidStreamerMode value) noexcept
{
    switch (value)
    {
        case EbloidStreamerMode::Disabled:
            return "Disabled";
        case EbloidStreamerMode::Blur:
            return "Blur (click to unblur)";
        case EbloidStreamerMode::ClickToLoad:
            return "Click to load (Ctrl+click opens link)";
    }
    return std::nullopt;
}

/// @brief Helpers for eblo.id file hosting links.
namespace ebloid {

/// @brief Returns true if @a url is an eblo.id post link.
///
/// Accepted forms: https://eblo.id/{post} (path must contain exactly one
/// segment and no query/fragment).
[[nodiscard]] bool isEbloidLink(const QString &url);

/// @brief Extracts the post id from an eblo.id link.
///
/// @pre @a url must be an eblo.id link, see #isEbloidLink().
[[nodiscard]] QString postIdFromLink(const QString &url);

/// @brief Returns the direct file download URL for a post id.
[[nodiscard]] QString fileDownloadUrl(const QString &postId);

/// @brief Returns the file download URL for an eblo.id link.
///
/// @pre @a url must be an eblo.id link, see #isEbloidLink().
[[nodiscard]] QString fileDownloadUrlFromLink(const QString &url);

/// @brief Returns true if @a loginName is in the trusted chatters list.
[[nodiscard]] bool isTrustedChatter(const QString &loginName);

}  // namespace ebloid

}  // namespace chatterino

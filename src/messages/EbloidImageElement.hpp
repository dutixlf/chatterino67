// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Ebloid.hpp"
#include "messages/MessageElement.hpp"

namespace chatterino {

/// @brief Inline image element used for eblo.id previews.
///
/// The image is rendered directly in the chat. Its height is capped at 320
/// logical pixels (at 100% scale) while preserving the aspect ratio.
///
/// In streamer mode the element may be blurred or hidden behind a placeholder
/// until the user clicks it.
class EbloidImageElement : public MessageElement
{
public:
    static constexpr std::string_view TYPE = "ebloid-image";

    /// @param image The loaded image. May be null for click-to-load mode.
    /// @param linkUrl The original eblo.id page link (e.g. https://eblo.id/abc).
    /// @param imageUrl The direct download URL (e.g.
    /// https://eblo.id/download/file/abc).
    /// @param mode The active streamer mode for this element.
    /// @param revealed If true, the image is shown normally (used for trusted
    /// chatters in streamer mode).
    EbloidImageElement(ImagePtr image, const QString &linkUrl,
                       const QString &imageUrl, EbloidStreamerMode mode,
                       bool revealed, MessageElementFlags flags);

    void addToContainer(MessageLayoutContainer &container,
                        const MessageLayoutContext &ctx) override;

    /// Updates the link based on the revealed state.
    void updateLink();

    bool isRevealed() const;
    const QString &linkUrl() const;

    /// Reveal the image (unblur or start loading). Requests a layout update.
    void reveal();

    QJsonObject toJson() const override;
    std::string_view type() const override;
    std::unique_ptr<MessageElement> clone() const override;

private:
    ImagePtr image_;
    QString linkUrl_;
    QString imageUrl_;
    EbloidStreamerMode mode_;
    bool revealed_;

    static constexpr int MAX_HEIGHT_100_PERCENT = 320;
};

}  // namespace chatterino

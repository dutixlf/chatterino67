// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Ebloid.hpp"
#include "messages/layouts/MessageLayoutElement.hpp"

namespace chatterino {

/// @brief Layout element that renders an eblo.id inline image.
///
/// The image can be shown normally, blurred, or replaced by a placeholder
/// depending on the streamer-mode state of the creator element.
class EbloidImageLayoutElement : public ImageLayoutElement
{
public:
    EbloidImageLayoutElement(MessageElement &creator, ImagePtr image,
                             QSizeF size, EbloidStreamerMode mode,
                             bool revealed);

protected:
    void addCopyTextToString(QString &str, uint32_t from = 0,
                             uint32_t to = UINT32_MAX) const override;
    void paint(QPainter &painter, const MessageColors &messageColors) override;
    bool paintAnimated(QPainter &painter, qreal yOffset) override;

private:
    EbloidStreamerMode mode_;
    bool revealed_;
};
}  // namespace chatterino

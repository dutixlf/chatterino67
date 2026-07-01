// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "messages/layouts/EbloidImageLayoutElement.hpp"

#include "common/Ebloid.hpp"
#include "common/Literals.hpp"
#include "messages/Image.hpp"

#include <QPainter>

using namespace chatterino::literals;

namespace chatterino {

EbloidImageLayoutElement::EbloidImageLayoutElement(MessageElement &creator,
                                                   ImagePtr image, QSizeF size,
                                                   EbloidStreamerMode mode,
                                                   bool revealed)
    : ImageLayoutElement(creator, image, size)
    , mode_(mode)
    , revealed_(revealed)
{
}

void EbloidImageLayoutElement::addCopyTextToString(QString &str, uint32_t from,
                                                   uint32_t to) const
{
    Q_UNUSED(from)
    Q_UNUSED(to)

    if (!this->revealed_)
    {
        str += u"[eblo.id image]"_s;
    }
    else
    {
        ImageLayoutElement::addCopyTextToString(str, from, to);
    }
}

void EbloidImageLayoutElement::paint(QPainter &painter,
                                     const MessageColors & /*messageColors*/)
{
    if (!this->revealed_)
    {
        const auto rect = this->getRect();
        painter.fillRect(rect, QColor(40, 40, 40));
        painter.setPen(QColor(160, 160, 160));
        painter.drawText(rect, Qt::AlignCenter, u"Click to load image"_s);
        return;
    }

    if (this->image_ == nullptr)
    {
        return;
    }

    auto pixmap = this->image_->pixmapOrLoad();
    if (!pixmap)
    {
        return;
    }

    painter.drawPixmap(QRectF(this->getRect()), *pixmap, QRectF());
}

bool EbloidImageLayoutElement::paintAnimated(QPainter &painter, qreal yOffset)
{
    if ((this->mode_ == EbloidStreamerMode::ClickToLoad ||
         this->mode_ == EbloidStreamerMode::Blur) &&
        !this->revealed_)
    {
        return false;
    }

    return ImageLayoutElement::paintAnimated(painter, yOffset);
}

}  // namespace chatterino

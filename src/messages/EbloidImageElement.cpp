// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "messages/EbloidImageElement.hpp"

#include "Application.hpp"
#include "common/Literals.hpp"
#include "messages/Image.hpp"
#include "messages/layouts/EbloidImageLayoutElement.hpp"
#include "messages/layouts/MessageLayoutContainer.hpp"
#include "messages/layouts/MessageLayoutContext.hpp"
#include "messages/layouts/MessageLayoutElement.hpp"
#include "singletons/WindowManager.hpp"

#include <QJsonObject>

#include <algorithm>

using namespace chatterino::literals;

namespace chatterino {

EbloidImageElement::EbloidImageElement(ImagePtr image, const QString &linkUrl,
                                       const QString &imageUrl,
                                       EbloidStreamerMode mode, bool revealed,
                                       MessageElementFlags flags)
    : MessageElement(flags)
    , image_(std::move(image))
    , linkUrl_(linkUrl)
    , imageUrl_(imageUrl)
    , mode_(mode)
    , revealed_(revealed)
{
    this->updateLink();
}

void EbloidImageElement::updateLink()
{
    if (this->revealed_)
    {
        if (this->image_ != nullptr)
        {
            this->setLink({Link::ViewImage, this->image_->url().string});
        }
        else if (!this->imageUrl_.isEmpty())
        {
            this->setLink({Link::ViewImage, this->imageUrl_});
        }
    }
    else if (this->mode_ == EbloidStreamerMode::Blur)
    {
        // Blur mode: the placeholder is clickable and reveals the image.
        this->setLink({Link::EbloidImage, this->linkUrl_});
    }
    else
    {
        // Click-to-load mode: the placeholder is not clickable; the link text
        // handles loading the image.
        this->setLink({});
    }
}

void EbloidImageElement::addToContainer(MessageLayoutContainer &container,
                                        const MessageLayoutContext &ctx)
{
    if (!ctx.flags.hasAny(this->getFlags()))
    {
        return;
    }

    if (this->mode_ == EbloidStreamerMode::ClickToLoad && !this->revealed_)
    {
        // No placeholder in click-to-load mode; the link text loads the image.
        return;
    }

    if (this->image_ != nullptr && this->image_->isEmpty())
    {
        return;
    }

    const auto scale = container.getScale();
    const auto maxHeight = static_cast<qreal>(MAX_HEIGHT_100_PERCENT) * scale;
    // Keep the image inside the message area (left/right margins are 8 px).
    const auto maxWidth = std::max(0.0, container.getWidth() - (8 + 8) * scale);

    QSizeF size;
    if (this->image_ != nullptr)
    {
        auto imageSize = this->image_->size();
        if (!imageSize.isEmpty())
        {
            auto naturalWidth = imageSize.width() * scale;
            auto naturalHeight = imageSize.height() * scale;
            const auto ratio = std::min(1.0, std::min(maxHeight / naturalHeight,
                                                      maxWidth / naturalWidth));
            size = QSizeF(naturalWidth * ratio, naturalHeight * ratio);
        }
    }

    container.addElement(new EbloidImageLayoutElement(
        *this, this->image_, size, this->mode_, this->revealed_));
}

bool EbloidImageElement::isRevealed() const
{
    return this->revealed_;
}

const QString &EbloidImageElement::linkUrl() const
{
    return this->linkUrl_;
}

void EbloidImageElement::reveal()
{
    if (this->revealed_)
    {
        return;
    }
    if (this->image_ == nullptr && !this->imageUrl_.isEmpty())
    {
        this->image_ = Image::fromUrl({this->imageUrl_}, 1, QSize(0, 0));
        this->image_->load();
    }
    this->revealed_ = true;
    this->updateLink();
    getApp()->getWindows()->forceLayoutChannelViews();
}

QJsonObject EbloidImageElement::toJson() const
{
    auto base = MessageElement::toJson();
    base["type"_L1] = u"EbloidImageElement"_s;
    base["url"_L1] = this->imageUrl_;
    base["link"_L1] = this->linkUrl_;

    return base;
}

std::string_view EbloidImageElement::type() const
{
    return EbloidImageElement::TYPE;
}

std::unique_ptr<MessageElement> EbloidImageElement::clone() const
{
    auto elem = std::make_unique<EbloidImageElement>(
        this->image_, this->linkUrl_, this->imageUrl_, this->mode_,
        this->revealed_, this->getFlags());
    elem->cloneFrom(*this);
    return elem;
}

}  // namespace chatterino

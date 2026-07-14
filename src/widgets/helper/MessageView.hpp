// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "messages/layouts/MessageLayoutContext.hpp"
#include "messages/Link.hpp"
#include "messages/Message.hpp"
#include "widgets/BaseWidget.hpp"

#include <QWidget>

namespace chatterino {

class MessageLayout;

/// MessageView is a fixed-width widget that displays a single message.
/// For the message to be rendered, you must call setWidth.
class MessageView : public BaseWidget
{
    Q_OBJECT

public:
    MessageView();
    ~MessageView() override;
    MessageView(const MessageView &) = delete;
    MessageView(MessageView &&) = delete;
    MessageView &operator=(const MessageView &) = delete;
    MessageView &operator=(MessageView &&) = delete;

    void setMessage(const MessagePtr &message);
    /// Like setMessage, but keeps the message's own elements (badges, emote
    /// images, username, ...) instead of flattening it to a single line of
    /// text. Used to preview a full chat message (e.g. the pinned message).
    void setFullMessage(const MessagePtr &message);
    void clearMessage();

    void setWidth(int width);

    /// Returns the Link at @a point (local coordinates), or an invalid Link.
    Link linkAt(QPointF point) const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void themeChangedEvent() override;
    void scaleChangedEvent(float newScale) override;

private:
    void createMessageLayout();
    void layoutMessage();

    MessagePtr message_;
    // When true, lay out with the normal chat word-flags (emote images etc.)
    // instead of the flattened single-line text flags.
    bool fullMessage_ = false;
    std::unique_ptr<MessageLayout> messageLayout_;

    MessageColors messageColors_;
    MessagePreferences messagePreferences_;

    int width_{};
};

}  // namespace chatterino

// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/settingspages/EbloidPage.hpp"

#include "common/Ebloid.hpp"
#include "singletons/Settings.hpp"
#include "util/LayoutCreator.hpp"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace chatterino {

EbloidPage::EbloidPage()
    : trustedListWidget_(nullptr)
{
    LayoutCreator<EbloidPage> layoutCreator(this);
    auto layout = layoutCreator.setLayoutType<QVBoxLayout>();
    layout->setAlignment(Qt::AlignTop);

    layout.emplace<QLabel>("Inline eblo.id images can be rendered below chat "
                           "messages. Use the options below to control how "
                           "they behave in streamer mode.");

    {
        auto *streamerModeLabel = new QLabel("Streamer mode");
        layout.append(streamerModeLabel);

        auto *streamerModeBox = new QComboBox();
        streamerModeBox->addItem("Disabled");
        streamerModeBox->addItem("Blur (click to unblur)");
        streamerModeBox->addItem("Click to load (Ctrl+click opens link)");
        streamerModeBox->setCurrentIndex(
            static_cast<int>(getSettings()->ebloidStreamerMode.getEnum()));
        QObject::connect(streamerModeBox,
                         QOverload<int>::of(&QComboBox::currentIndexChanged),
                         [](int index) {
                             getSettings()->ebloidStreamerMode =
                                 static_cast<EbloidStreamerMode>(index);
                         });
        layout.append(streamerModeBox);
    }

    layout.emplace<QLabel>(
        "<b>Disabled</b>: images are loaded and shown normally.<br>"
        "<b>Blur</b>: images are loaded but shown blurred; click them to "
        "unblur.<br>"
        "<b>Click to load</b>: images are not loaded until clicked; Ctrl+click "
        "opens the eblo.id page.",
        nullptr);

    {
        auto *trustedGroup = new QGroupBox("Trusted chatters");
        auto *trustedLayout = new QVBoxLayout(trustedGroup);

        trustedLayout->addWidget(new QLabel(
            "For click-to-load mode, images from these users are shown "
            "instantly."));

        this->trustedListWidget_ = new QListWidget();
        for (const auto &user :
             getSettings()->ebloidStreamerTrustedChatters.getValue())
        {
            this->trustedListWidget_->addItem(user);
        }
        trustedLayout->addWidget(this->trustedListWidget_);

        {
            auto *addLayout = new QHBoxLayout();
            auto *userInput = new QLineEdit();
            userInput->setPlaceholderText("username");
            auto *addButton = new QPushButton("Add");
            QObject::connect(
                addButton, &QPushButton::clicked, [this, userInput]() {
                    const auto user = userInput->text().trimmed().toLower();
                    if (user.isEmpty())
                    {
                        return;
                    }
                    // Avoid duplicates.
                    auto items = this->trustedListWidget_->findItems(
                        user, Qt::MatchExactly);
                    if (!items.isEmpty())
                    {
                        return;
                    }
                    this->trustedListWidget_->addItem(user);
                    auto trusted =
                        getSettings()->ebloidStreamerTrustedChatters.getValue();
                    trusted.push_back(user);
                    getSettings()->ebloidStreamerTrustedChatters = trusted;
                    userInput->clear();
                });
            addLayout->addWidget(userInput);
            addLayout->addWidget(addButton);
            trustedLayout->addLayout(addLayout);
        }

        {
            auto *removeButton = new QPushButton("Remove selected");
            QObject::connect(removeButton, &QPushButton::clicked, [this]() {
                for (auto *item : this->trustedListWidget_->selectedItems())
                {
                    delete this->trustedListWidget_->takeItem(
                        this->trustedListWidget_->row(item));
                }
                std::vector<QString> trusted;
                for (int i = 0; i < this->trustedListWidget_->count(); ++i)
                {
                    trusted.push_back(
                        this->trustedListWidget_->item(i)->text());
                }
                getSettings()->ebloidStreamerTrustedChatters = trusted;
            });
            trustedLayout->addWidget(removeButton);
        }

        layout.append(trustedGroup);
    }

    layout->addStretch(1);
}

}  // namespace chatterino

// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "widgets/settingspages/SettingsPage.hpp"

class QListWidget;

namespace chatterino {

class EbloidPage : public SettingsPage
{
    Q_OBJECT

public:
    EbloidPage();

private:
    QListWidget *trustedListWidget_;
};

}  // namespace chatterino

#pragma once

#include "widgets/settingspages/SettingsPage.hpp"

class QListWidget;

namespace chatterino {

class AntispamPage : public SettingsPage
{
    Q_OBJECT

public:
    AntispamPage();

private:
    QListWidget *exceptionsList_{};
};

}  // namespace chatterino

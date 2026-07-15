// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "controllers/hotkeys/Hotkey.hpp"

#include <QDialog>

#include <memory>

class QComboBox;
class QKeySequenceEdit;
class QLineEdit;

namespace chatterino {

/// Dialog for adding a moderation quick-action keybind.
class AddModKeybindDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit AddModKeybindDialog(QWidget *parent = nullptr);

    /// Pre-fill from an existing hotkey for editing.
    void setExisting(const std::shared_ptr<Hotkey> &hk);

    /// Returns the hotkey the user configured, or nullptr on cancel.
    std::shared_ptr<Hotkey> data() const;

private:
    QLineEdit *nameEdit_ = nullptr;
    QComboBox *actionPicker_ = nullptr;
    QLineEdit *timeEdit_ = nullptr;
    QKeySequenceEdit *keyComboEdit_ = nullptr;
};

}  // namespace chatterino

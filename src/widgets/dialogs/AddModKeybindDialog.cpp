// SPDX-FileCopyrightText: 2025 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/AddModKeybindDialog.hpp"

#include "controllers/hotkeys/HotkeyController.hpp"
#include "controllers/hotkeys/HotkeyHelpers.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>

namespace chatterino {

namespace {

/// Convert "01h30m00s" → total seconds. Returns -1 on parse error.
int parseDurationString(const QString &str)
{
    static QRegularExpression re(
        R"(^(?:(\d+)h)?(?:(\d+)m)?(?:(\d+)s)?$)");
    auto match = re.match(str.trimmed());
    if (!match.hasMatch())
    {
        return -1;
    }
    int h = match.captured(1).toInt();
    int m = match.captured(2).toInt();
    int s = match.captured(3).toInt();
    if (h == 0 && m == 0 && s == 0)
    {
        return -1;  // must have at least something
    }
    return h * 3600 + m * 60 + s;
}

/// Convert seconds → "01h30m00s"
QString durationToString(int totalSeconds)
{
    int h = totalSeconds / 3600;
    int m = (totalSeconds % 3600) / 60;
    int s = totalSeconds % 60;
    return QString("%1h%2m%3s")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}

constexpr const char *ACTION_DELETE = "modDeleteHoveredMessage";
constexpr const char *ACTION_TIMEOUT = "modTimeoutHoveredUser";
constexpr const char *ACTION_BAN = "modBanHoveredUser";
constexpr const char *ACTION_UNBAN = "modUnbanHoveredUser";

}  // namespace

AddModKeybindDialog::AddModKeybindDialog(QWidget *parent)
    : QDialog(parent, Qt::WindowStaysOnTopHint)
{
    this->setWindowTitle("Add moderation keybind");
    this->setMinimumWidth(380);

    auto *layout = new QFormLayout(this);

    // Name
    this->nameEdit_ = new QLineEdit(this);
    this->nameEdit_->setPlaceholderText("e.g. Quick ban");
    layout->addRow("Name:", this->nameEdit_);

    // Action
    this->actionPicker_ = new QComboBox(this);
    this->actionPicker_->addItem("Delete message", ACTION_DELETE);
    this->actionPicker_->addItem("Timeout user", ACTION_TIMEOUT);
    this->actionPicker_->addItem("Ban user", ACTION_BAN);
    this->actionPicker_->addItem("Unban user", ACTION_UNBAN);
    layout->addRow("Action:", this->actionPicker_);

    // Time (timeout only)
    this->timeEdit_ = new QLineEdit(this);
    this->timeEdit_->setPlaceholderText("e.g. 00h10m00s");
    this->timeEdit_->setValidator(new QRegularExpressionValidator(
        QRegularExpression(R"(^[\dhmsHMS]*$)"), this));
    layout->addRow("Time:", this->timeEdit_);

    auto updateTimeVisibility = [this](int index) {
        auto action = this->actionPicker_->itemData(index).toString();
        this->timeEdit_->setVisible(action == ACTION_TIMEOUT);
        this->timeEdit_->parentWidget()
            ->findChild<QLabel *>("timeLabel")
            ->setVisible(action == ACTION_TIMEOUT);
    };
    // Find the time label we just added
    for (auto *lbl : this->findChildren<QLabel *>())
    {
        if (lbl->text() == "Time:")
        {
            lbl->setObjectName("timeLabel");
        }
    }
    QObject::connect(this->actionPicker_,
                     QOverload<int>::of(&QComboBox::currentIndexChanged),
                     this, updateTimeVisibility);
    updateTimeVisibility(0);

    // Keybind
    this->keyComboEdit_ = new QKeySequenceEdit(this);
    QObject::connect(
        this->keyComboEdit_, &QKeySequenceEdit::keySequenceChanged, this,
        [this](const QKeySequence &seq) {
            auto normalized = normalizeKeySequence(seq);
            if (normalized != seq)
            {
                QSignalBlocker blocker(this->keyComboEdit_);
                this->keyComboEdit_->setKeySequence(normalized);
            }
        });
    layout->addRow("Keybind:", this->keyComboEdit_);

    // Buttons
    auto *btnRow = new QHBoxLayout();
    auto *okBtn = new QPushButton("OK", this);
    auto *cancelBtn = new QPushButton("Cancel", this);
    btnRow->addStretch();
    btnRow->addWidget(okBtn);
    btnRow->addWidget(cancelBtn);
    layout->addRow(btnRow);

    QObject::connect(okBtn, &QPushButton::clicked, this, [this]() {
        this->accept();
    });
    QObject::connect(cancelBtn, &QPushButton::clicked, this, [this]() {
        this->reject();
    });
}

void AddModKeybindDialog::setExisting(const std::shared_ptr<Hotkey> &hk)
{
    if (!hk)
    {
        return;
    }

    this->setWindowTitle("Edit moderation keybind");

    {
        QSignalBlocker b(this->nameEdit_);
        this->nameEdit_->setText(hk->name());
    }

    // Select matching action
    auto action = hk->action();
    for (int i = 0; i < this->actionPicker_->count(); ++i)
    {
        if (this->actionPicker_->itemData(i).toString() == action)
        {
            QSignalBlocker b(this->actionPicker_);
            this->actionPicker_->setCurrentIndex(i);
            break;
        }
    }

    // Populate time for timeout
    if (action == ACTION_TIMEOUT && !hk->arguments().empty())
    {
        int secs = hk->arguments()[0].toInt();
        if (secs > 0)
        {
            QSignalBlocker b(this->timeEdit_);
            this->timeEdit_->setText(durationToString(secs));
        }
    }

    // Populate keybind
    {
        QSignalBlocker b(this->keyComboEdit_);
        this->keyComboEdit_->setKeySequence(hk->keySequence());
    }
}

std::shared_ptr<Hotkey> AddModKeybindDialog::data() const
{
    auto seq = this->keyComboEdit_->keySequence();
    if (seq.isEmpty())
    {
        return nullptr;
    }

    auto actionName = this->actionPicker_->currentData().toString();
    auto name = this->nameEdit_->text().trimmed();
    if (name.isEmpty())
    {
        name = this->actionPicker_->currentText().toLower();
    }

    std::vector<QString> args;

    if (actionName == ACTION_TIMEOUT)
    {
        int seconds = parseDurationString(this->timeEdit_->text());
        if (seconds <= 0)
        {
            seconds = 600;  // default 10 minutes
        }
        args.push_back(QString::number(seconds));
    }
    return std::make_shared<Hotkey>(HotkeyCategory::Split, seq, actionName,
                                    args, name);
}

}  // namespace chatterino

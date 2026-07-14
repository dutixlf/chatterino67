#include "widgets/settingspages/AntispamPage.hpp"

#include "singletons/Settings.hpp"
#include "util/LayoutCreator.hpp"
#include "widgets/dialogs/ColorPickerDialog.hpp"
#include "widgets/helper/color/ColorButton.hpp"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace chatterino {

namespace {
void addColorPicker(QVBoxLayout *parent, const QString &label,
                    QStringSetting &setting, QWidget *host,
                    pajlada::Signals::SignalHolder &connections)
{
    auto *row = new QHBoxLayout();
    row->addWidget(new QLabel(label));

    auto *btn = new ColorButton(QColor(setting.getValue()));
    setting.connect(
        [btn](const QString &v, const auto &) {
            btn->setColor(QColor(v));
        },
        connections);
    QObject::connect(btn, &ColorButton::clicked, [host, &setting]() {
        auto *dlg = new ColorPickerDialog(QColor(setting), host);
        QObject::connect(dlg, &ColorPickerDialog::colorConfirmed, host,
                         [&setting](auto selected) {
                             if (selected.isValid())
                             {
                                 setting =
                                     selected.name(QColor::HexArgb);
                             }
                         });
        dlg->show();
    });
    row->addWidget(btn);
    row->addStretch(1);
    parent->addLayout(row);
}
}  // namespace

AntispamPage::AntispamPage()
{
    LayoutCreator<AntispamPage> layoutCreator(this);
    auto layout = layoutCreator.setLayoutType<QVBoxLayout>();
    layout->setAlignment(Qt::AlignTop);

    layout.emplace<QLabel>(
        "Detects and highlights spam messages (repeated identical messages "
        "from one or multiple users) within a configurable time window.\n\n"
        "<b>Spam</b> = same user repeating the same message.\n"
        "<b>Pasta</b> = multiple different users sending the same message.");

    {
        auto *enableCb = this->createCheckBox(
            "Enable antispam detection", getSettings()->enableAntispam);
        layout.append(enableCb);
    }

    {
        auto *grp = new QGroupBox("Detect");
        auto *grpLayout = new QVBoxLayout(grp);
        grpLayout->addWidget(this->createCheckBox(
            "Spam (same user repeating a message)",
            getSettings()->antispamDetectSpam));
        grpLayout->addWidget(this->createCheckBox(
            "Pasta (different users sending the same message)",
            getSettings()->antispamDetectPasta));
        layout.append(grp);
    }

    {
        auto *form = new QFormLayout();

        auto *threshold = new QSpinBox();
        threshold->setRange(2, 50);
        threshold->setValue(getSettings()->antispamThreshold);
        threshold->setToolTip(
            "Number of similar messages needed to trigger detection. "
            "Default: 3");
        QObject::connect(threshold,
                         QOverload<int>::of(&QSpinBox::valueChanged),
                         [](int v) {
                             getSettings()->antispamThreshold = v;
                         });
        form->addRow("Threshold:", threshold);

        auto *window = new QSpinBox();
        window->setRange(1, 300);
        window->setValue(getSettings()->antispamWindowSeconds);
        window->setSuffix(" s");
        window->setToolTip(
            "Time window in seconds to check for repeated messages. "
            "Default: 10");
        QObject::connect(window,
                         QOverload<int>::of(&QSpinBox::valueChanged),
                         [](int v) {
                             getSettings()->antispamWindowSeconds = v;
                         });
        form->addRow("Time window:", window);

        layout->addLayout(form);
    }

    // Colors
    {
        auto *grp = new QGroupBox("Highlight colors");
        auto *grpLayout = new QVBoxLayout(grp);

        addColorPicker(grpLayout, "Spam color (per-user):",
                       getSettings()->antispamColor, this,
                       this->managedConnections_);
        addColorPicker(grpLayout, "Pasta color (cross-user):",
                       getSettings()->antispamPastaColor, this,
                       this->managedConnections_);

        layout.append(grp);
    }

    // Ignore options
    {
        auto *grp = new QGroupBox("Ignore");
        auto *grpLayout = new QVBoxLayout(grp);

        grpLayout->addWidget(this->createCheckBox(
            "Ignore VIPs", getSettings()->antispamIgnoreVIPs));
        grpLayout->addWidget(this->createCheckBox(
            "Ignore moderators", getSettings()->antispamIgnoreMods));

        layout.append(grp);
    }

    // Exceptions
    {
        auto *grp = new QGroupBox("Exceptions");
        auto *grpLayout = new QVBoxLayout(grp);

        grpLayout->addWidget(new QLabel(
            "Messages from these users will not be flagged as spam."));

        this->exceptionsList_ = new QListWidget();
        for (const auto &u : getSettings()->antispamExceptions.getValue())
        {
            this->exceptionsList_->addItem(u);
        }
        grpLayout->addWidget(this->exceptionsList_);

        {
            auto *row = new QHBoxLayout();
            auto *input = new QLineEdit();
            input->setPlaceholderText("username");
            auto *addBtn = new QPushButton("Add");
            QObject::connect(
                addBtn, &QPushButton::clicked, [this, input]() {
                    auto user = input->text().trimmed().toLower();
                    if (user.isEmpty())
                        return;
                    if (!this->exceptionsList_
                             ->findItems(user, Qt::MatchExactly)
                             .isEmpty())
                        return;
                    this->exceptionsList_->addItem(user);
                    auto vals =
                        getSettings()->antispamExceptions.getValue();
                    vals.push_back(user);
                    getSettings()->antispamExceptions = vals;
                    input->clear();
                });
            row->addWidget(input);
            row->addWidget(addBtn);
            grpLayout->addLayout(row);
        }

        {
            auto *rmBtn = new QPushButton("Remove selected");
            QObject::connect(rmBtn, &QPushButton::clicked, [this]() {
                for (auto *item : this->exceptionsList_->selectedItems())
                {
                    delete this->exceptionsList_->takeItem(
                        this->exceptionsList_->row(item));
                }
                std::vector<QString> vals;
                for (int i = 0; i < this->exceptionsList_->count(); ++i)
                {
                    vals.push_back(
                        this->exceptionsList_->item(i)->text());
                }
                getSettings()->antispamExceptions = vals;
            });
            grpLayout->addWidget(rmBtn);
        }

        layout.append(grp);
    }

    layout->addStretch(1);
}

}  // namespace chatterino

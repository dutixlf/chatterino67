// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/Updates.hpp"

#include "common/Literals.hpp"
#include "common/Modes.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "common/Version.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "util/CombinePath.hpp"
#include "util/PostToThread.hpp"

#include <QApplication>
#include <QDesktopServices>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QStringBuilder>
#include <QtConcurrent>
#include <semver/semver.hpp>

namespace {

using namespace chatterino;
using namespace literals;

constexpr auto GITHUB_REPO = "dutixlf/chatterino67";
constexpr auto GITHUB_BRANCH = "chatterino67";
constexpr auto NIGHTLY_TAG = "nightly-build";

bool shortHashMatches(const QString &remoteSha, const QString &localHash)
{
    if (remoteSha.isEmpty() || localHash.isEmpty())
    {
        return false;
    }
    // localHash from git rev-parse --short HEAD (7 chars typically)
    // remoteSha is first 7 chars of full SHA
    return remoteSha.compare(localHash.left(remoteSha.length()),
                             Qt::CaseInsensitive) == 0;
}

}  // namespace

namespace chatterino {

Updates::Updates(const Paths &paths_, Settings &settings)
    : paths(paths_)
    , currentVersion_(CHATTERINO_VERSION)
    , updateGuideLink_("https://chatterino.com")
{
    qCDebug(chatterinoUpdate) << "init UpdateManager";

    settings.betaUpdates.connect(
        [this] {
            this->checkForUpdates();
        },
        this->managedConnections, false);
}

bool Updates::isDowngradeOf(const QString &online, const QString &current)
{
    semver::version onlineVersion;
    if (!onlineVersion.from_string_noexcept(online.toStdString()))
    {
        return false;
    }

    semver::version currentVersion;
    if (!currentVersion.from_string_noexcept(current.toStdString()))
    {
        return false;
    }

    if (onlineVersion.major == 7)
    {
        onlineVersion.major = 2;
    }

    return onlineVersion < currentVersion;
}

void Updates::deleteOldFiles()
{
    std::ignore = QtConcurrent::run([dir{this->paths.miscDirectory}] {
        {
            auto path = combinePath(dir, "Update.exe");
            if (QFile::exists(path))
            {
                QFile::remove(path);
            }
        }
        {
            auto path = combinePath(dir, "update.zip");
            if (QFile::exists(path))
            {
                QFile::remove(path);
            }
        }
    });
}

const QString &Updates::getCurrentVersion() const
{
    return this->currentVersion_;
}

const QString &Updates::getOnlineVersion() const
{
    return this->onlineVersion_;
}

void Updates::installUpdates()
{
    if (this->status_ != UpdateAvailable)
    {
        assert(false);
        return;
    }

    if (Version::instance().isNightly())
    {
        // ponytail: nightly builds use in-app updater, not browser
    }

#ifdef Q_OS_MACOS
    QMessageBox *box = new QMessageBox(
        QMessageBox::Information, "Chatterino Update",
        "A link will open in your browser. Download and install to update.");
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
    QDesktopServices::openUrl(this->updateExe_);
#elif defined Q_OS_LINUX
    QMessageBox *box =
        new QMessageBox(QMessageBox::Information, "Chatterino Update",
                        "Automatic updates are currently not available on "
                        "Linux. Please redownload the app to update.");
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
    QDesktopServices::openUrl(this->updateGuideLink_);
#elif defined Q_OS_WIN
    if (Modes::instance().isPortable)
    {
        QMessageBox *box =
            new QMessageBox(QMessageBox::Information, "Chatterino Update",
                            "Chatterino is downloading the update "
                            "in the background and will run the "
                            "updater once it is finished.");
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->show();

        NetworkRequest(this->updatePortable_)
            .timeout(600000)
            .followRedirects(true)
            .onError([this](NetworkResult) {
                this->setStatus_(DownloadFailed);

                postToThread([] {
                    QMessageBox *box = new QMessageBox(
                        QMessageBox::Information, "Chatterino Update",
                        "Failed while trying to download the update.");
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->show();
                    box->raise();
                });
            })
            .onSuccess([this](auto result) {
                if (result.status() != 200)
                {
                    auto *box = new QMessageBox(
                        QMessageBox::Information, "Chatterino Update",
                        QStringLiteral("The update couldn't be downloaded "
                                       "(Error: %1).")
                            .arg(result.formatError()));
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->exec();
                    return;
                }

                QByteArray object = result.getData();
                auto filename =
                    combinePath(this->paths.miscDirectory, "update.zip");

                QFile file(filename);
                if (!file.open(QIODevice::Truncate | QIODevice::WriteOnly))
                {
                    qCWarning(chatterinoUpdate)
                        << "Failed to save update.zip" << file.errorString();
                    this->setStatus_(WriteFileFailed);
                    return;
                }

                if (file.write(object) == -1)
                {
                    this->setStatus_(WriteFileFailed);
                    return;
                }
                file.flush();
                file.close();

                auto updaterPath = Updates::portableUpdaterPath();
                if (!QFile::exists(updaterPath))
                {
                    this->setStatus_(MissingPortableUpdater);
                    return;
                }
                bool ok =
                    QProcess::startDetached(updaterPath, {filename, "restart"});
                if (!ok)
                {
                    this->setStatus_(RunUpdaterFailed);
                    return;
                }

                QApplication::exit(0);
            })
            .execute();
        this->setStatus_(Downloading);
    }
    else
    {
        QMessageBox *box =
            new QMessageBox(QMessageBox::Information, "Chatterino Update",
                            "Chatterino is downloading the update "
                            "in the background and will run the "
                            "updater once it is finished.");
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->show();

        NetworkRequest(this->updateExe_)
            .timeout(600000)
            .followRedirects(true)
            .onError([this](NetworkResult) {
                this->setStatus_(DownloadFailed);

                QMessageBox *box = new QMessageBox(
                    QMessageBox::Information, "Chatterino Update",
                    "Failed to download the update. \n\nTry manually "
                    "downloading the update.");
                box->setAttribute(Qt::WA_DeleteOnClose);
                box->exec();
            })
            .onSuccess([this](auto result) {
                if (result.status() != 200)
                {
                    auto *box = new QMessageBox(
                        QMessageBox::Information, "Chatterino Update",
                        QStringLiteral("The update couldn't be downloaded "
                                       "(Error: %1).")
                            .arg(result.formatError()));
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->exec();
                    return;
                }

                QByteArray object = result.getData();
                auto filePath =
                    combinePath(this->paths.miscDirectory, "Update.exe");

                QFile file(filePath);
                std::ignore =
                    file.open(QIODevice::Truncate | QIODevice::WriteOnly);

                if (file.write(object) == -1)
                {
                    this->setStatus_(WriteFileFailed);
                    QMessageBox *box = new QMessageBox(
                        QMessageBox::Information, "Chatterino Update",
                        "Failed to save the update file. This could be due to "
                        "window settings or antivirus software.\n\nTry "
                        "manually "
                        "downloading the update.");
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->exec();

                    QDesktopServices::openUrl(this->updateExe_);
                    return;
                }
                file.flush();
                file.close();

                if (QProcess::startDetached(filePath, {}))
                {
                    QApplication::exit(0);
                }
                else
                {
                    QMessageBox *box = new QMessageBox(
                        QMessageBox::Information, "Chatterino Update",
                        "Failed to execute update binary. This could be due to "
                        "window "
                        "settings or antivirus software.\n\nTry manually "
                        "downloading "
                        "the update.");
                    box->setAttribute(Qt::WA_DeleteOnClose);
                    box->exec();

                    QDesktopServices::openUrl(this->updateExe_);
                }
            })
            .execute();
        this->setStatus_(Downloading);
    }
#endif
}

void Updates::checkForUpdates()
{
#ifndef CHATTERINO_DISABLE_UPDATER
    auto version = Version::instance();

    if (!version.isSupportedOS())
    {
        qCDebug(chatterinoUpdate)
            << "Update checking disabled because OS doesn't appear to be one "
               "of Windows, GNU/Linux or macOS.";
        return;
    }

    if (version.isFlatpak())
    {
        return;
    }

    auto *self = this;

    // Step 1: Get latest commit SHA from the branch
    auto commitUrl = u"https://api.github.com/repos/"_s % GITHUB_REPO %
                     u"/commits/"_s % GITHUB_BRANCH;
    qCDebug(chatterinoUpdate) << "Requesting latest commit from" << commitUrl;

    NetworkRequest(commitUrl)
        .header("Accept", "application/vnd.github+json")
        .timeout(60000)
        .followRedirects(true)
        .onSuccess([self](const NetworkResult &result) {
            const auto obj = result.parseJson();
            auto sha = obj["sha"].toString();
            if (sha.isEmpty())
            {
                self->setStatus_(SearchFailed);
                return;
            }

            auto shortSha = sha.left(7);
            auto currentHash = Version::instance().commitHash();

            if (shortHashMatches(shortSha, currentHash))
            {
                self->setStatus_(NoUpdateAvailable);
                return;
            }

            self->onlineVersion_ = shortSha;

            // Step 2: Get release assets
            self->fetchReleaseAssets();
        })
        .onError([self](const NetworkResult &) {
            self->setStatus_(SearchFailed);
        })
        .execute();

    this->setStatus_(Searching);
#endif
}

void Updates::fetchReleaseAssets()
{
    auto *self = this;
    auto releaseUrl = u"https://api.github.com/repos/"_s % GITHUB_REPO %
                      u"/releases/tags/"_s % NIGHTLY_TAG;
    qCDebug(chatterinoUpdate) << "Requesting release assets from" << releaseUrl;

    NetworkRequest(releaseUrl)
        .header("Accept", "application/vnd.github+json")
        .timeout(60000)
        .followRedirects(true)
        .onSuccess([self](const NetworkResult &result) {
            const auto obj = result.parseJson();
            auto assets = obj["assets"].toArray();

            QString installerUrl;
            QString portableUrl;
            QString rawWindowsUrl;

            for (const auto &assetVal : assets)
            {
                auto asset = assetVal.toObject();
                auto name = asset["name"].toString();
                auto url = asset["browser_download_url"].toString();

#if defined(Q_OS_WIN)
#    ifdef Q_PROCESSOR_ARM
                if (name.contains("ARM64", Qt::CaseInsensitive) &&
                    name.contains("Installer", Qt::CaseInsensitive) &&
                    name.endsWith(".exe"))
                {
                    installerUrl = url;
                }
                if (name.contains("ARM64", Qt::CaseInsensitive) &&
                    name.contains("Portable", Qt::CaseInsensitive) &&
                    name.endsWith(".zip"))
                {
                    portableUrl = url;
                }
#    else
                if (!name.contains("ARM64", Qt::CaseInsensitive) &&
                    name.contains("Installer", Qt::CaseInsensitive) &&
                    name.endsWith(".exe"))
                {
                    installerUrl = url;
                }
                if (!name.contains("ARM64", Qt::CaseInsensitive) &&
                    name.contains("Portable", Qt::CaseInsensitive) &&
                    name.endsWith(".zip"))
                {
                    portableUrl = url;
                }
#    endif
                if (name.contains("windows", Qt::CaseInsensitive) &&
                    name.endsWith(".zip") && rawWindowsUrl.isEmpty())
                {
                    rawWindowsUrl = url;
                }
#endif
#if defined(Q_OS_MACOS)
                if (name.endsWith(".dmg"))
                {
                    installerUrl = url;
                }
#endif
            }

#if defined(Q_OS_WIN)
            if (installerUrl.isEmpty())
            {
                qCWarning(chatterinoUpdate)
                    << "No installer asset found in nightly release";
            }
            if (portableUrl.isEmpty())
            {
                portableUrl = rawWindowsUrl;
            }
            self->updateExe_ = installerUrl;
            self->updatePortable_ = portableUrl;
#elif defined(Q_OS_MACOS)
            self->updateExe_ = installerUrl;
#endif
            self->updateGuideLink_ =
                u"https://github.com/"_s % GITHUB_REPO % u"/releases"_s;

            if (self->updateExe_.isEmpty() && self->updatePortable_.isEmpty())
            {
                self->setStatus_(SearchFailed);
                return;
            }

            self->setStatus_(UpdateAvailable);
        })
        .onError([self](const NetworkResult &) {
            self->setStatus_(SearchFailed);
        })
        .execute();
}

Updates::Status Updates::getStatus() const
{
    return this->status_;
}

QString Updates::portableUpdaterPath()
{
    return combinePath(QCoreApplication::applicationDirPath(),
                       "updater.1/ChatterinoUpdater.exe");
}

bool Updates::shouldShowUpdateButton() const
{
    switch (this->getStatus())
    {
        case UpdateAvailable:
        case SearchFailed:
        case Downloading:
        case DownloadFailed:
        case WriteFileFailed:
            return true;

        default:
            return false;
    }
}

bool Updates::isError() const
{
    switch (this->getStatus())
    {
        case SearchFailed:
        case DownloadFailed:
        case WriteFileFailed:
        case MissingPortableUpdater:
        case RunUpdaterFailed:
            return true;

        default:
            return false;
    }
}

bool Updates::isDowngrade() const
{
    return this->isDowngrade_;
}

QString Updates::buildUpdateAvailableText() const
{
    const auto &version = Version::instance();

    if (version.isNightly())
    {
        return QString("A new nightly build (%1) is available.\n\n"
                       "Do you want to download and install it?")
            .arg(this->getOnlineVersion());
    }

    if (this->isDowngrade())
    {
        return QString("The version online (%1) seems to be lower than the "
                       "current (%2).\nEither a version was reverted or "
                       "you are running a newer build.\n\nDo you want to "
                       "download and install it?")
            .arg(this->getOnlineVersion(), this->getCurrentVersion());
    }

    return QString("An update (%1) is available.\n\nDo you want to "
                   "download and install it?")
        .arg(this->getOnlineVersion());
}

void Updates::setStatus_(Status status)
{
    if (this->status_ != status)
    {
        this->status_ = status;
        postToThread([this, status] {
            this->statusUpdated.invoke(status);
        });
    }
}

}  // namespace chatterino

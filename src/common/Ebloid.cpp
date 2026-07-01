// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/Ebloid.hpp"

#include "singletons/Settings.hpp"

#include <QUrl>

#include <algorithm>

namespace chatterino {
namespace ebloid {

namespace {

constexpr QStringView HOST = u"eblo.id";
constexpr QStringView SCHEME = u"https";

}  // namespace

bool isEbloidLink(const QString &url)
{
    QUrl parsed(url);
    if (!parsed.isValid())
    {
        return false;
    }

    if (parsed.scheme().compare(SCHEME, Qt::CaseInsensitive) != 0)
    {
        return false;
    }

    const QString host = parsed.host().toLower();
    if (host != HOST)
    {
        return false;
    }

    if (!parsed.query().isEmpty() || !parsed.fragment().isEmpty())
    {
        return false;
    }

    const QString path = parsed.path();
    // must be "/{post}" - exactly one path segment
    if (path.length() < 2 || path.startsWith(QLatin1String("//")))
    {
        return false;
    }

    const auto postId = path.mid(1);
    if (postId.isEmpty())
    {
        return false;
    }
    if (postId.contains(u'/'))
    {
        return false;
    }

    return true;
}

QString postIdFromLink(const QString &url)
{
    QUrl parsed(url);
    const QString path = parsed.path();
    return path.mid(1);
}

QString fileDownloadUrl(const QString &postId)
{
    return QStringLiteral("https://eblo.id/download/file/%1").arg(postId);
}

QString fileDownloadUrlFromLink(const QString &url)
{
    return fileDownloadUrl(postIdFromLink(url));
}

bool isTrustedChatter(const QString &loginName)
{
    if (loginName.isEmpty())
    {
        return false;
    }
    const auto &trusted =
        getSettings()->ebloidStreamerTrustedChatters.getValue();
    return std::any_of(
        trusted.begin(), trusted.end(), [&loginName](const QString &user) {
            return user.compare(loginName, Qt::CaseInsensitive) == 0;
        });
}

}  // namespace ebloid

}  // namespace chatterino

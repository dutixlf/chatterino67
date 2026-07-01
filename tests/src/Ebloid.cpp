// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "common/Ebloid.hpp"

#include "common/Literals.hpp"
#include "singletons/Settings.hpp"
#include "Test.hpp"

#include <QString>

using namespace chatterino;
using namespace chatterino::literals;

TEST(Ebloid, isEbloidLink)
{
    EXPECT_TRUE(ebloid::isEbloidLink("https://eblo.id/abc123"));
    EXPECT_TRUE(ebloid::isEbloidLink("https://eblo.id/abc_123"));
    EXPECT_FALSE(ebloid::isEbloidLink("https://eblo.id/"));
    EXPECT_FALSE(ebloid::isEbloidLink("https://eblo.id/abc/123"));
    EXPECT_FALSE(ebloid::isEbloidLink("http://eblo.id/abc123"));
    EXPECT_FALSE(ebloid::isEbloidLink("https://evil.eblo.id/abc123"));
    EXPECT_FALSE(ebloid::isEbloidLink("https://eblo.id/abc123?extra"));
    EXPECT_FALSE(ebloid::isEbloidLink("https://not-eblo.id/abc123"));
}

TEST(Ebloid, postIdFromLink)
{
    EXPECT_EQ(ebloid::postIdFromLink("https://eblo.id/abc123"), "abc123");
}

TEST(Ebloid, fileDownloadUrl)
{
    EXPECT_EQ(ebloid::fileDownloadUrl("abc123"),
              "https://eblo.id/download/file/abc123");
}

TEST(Ebloid, fileDownloadUrlFromLink)
{
    EXPECT_EQ(ebloid::fileDownloadUrlFromLink("https://eblo.id/abc123"),
              "https://eblo.id/download/file/abc123");
}

TEST(Ebloid, isTrustedChatter)
{
    getSettings()->ebloidStreamerTrustedChatters =
        std::vector<QString>{u"trusted_one"_s, u"Trusted_Two"_s};

    EXPECT_TRUE(ebloid::isTrustedChatter(u"trusted_one"_s));
    EXPECT_TRUE(ebloid::isTrustedChatter(u"TRUSTED_ONE"_s));
    EXPECT_TRUE(ebloid::isTrustedChatter(u"trusted_two"_s));
    EXPECT_FALSE(ebloid::isTrustedChatter(u"not_trusted"_s));
    EXPECT_FALSE(ebloid::isTrustedChatter(u""_s));
}

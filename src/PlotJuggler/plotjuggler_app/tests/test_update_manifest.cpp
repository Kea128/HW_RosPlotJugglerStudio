#include "studio/update/update_manifest.h"

#include <gtest/gtest.h>
#include <iterator>

using StudioUpdate::SemVer;
using StudioUpdate::UpdateManifest;

namespace
{
QByteArray validManifest()
{
  return R"({
    "schemaVersion": 1,
    "version": "3.18.0-beta.2+build.5",
    "platform": "windows",
    "arch": "x86_64",
    "channel": "beta",
    "publishedAt": "2026-08-20T12:00:00Z",
    "minimumSupportedVersion": "3.17.0",
    "assetUrl": "https://github.com/Kea128/HW_RosPlotJugglerStudio/releases/download/v3.18.0/update.zip",
    "releaseNotesUrl": "https://github.com/Kea128/HW_RosPlotJugglerStudio/releases/tag/v3.18.0",
    "size": 123456,
    "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
  })";
}

SemVer semver(const char* text)
{
  SemVer value;
  EXPECT_TRUE(SemVer::parse(QString::fromLatin1(text), &value));
  return value;
}
}  // namespace

TEST(SemVer, ParsesAndFormatsFullVersion)
{
  SemVer value;
  QString error;
  ASSERT_TRUE(SemVer::parse("3.17.2-studio.1+portable", &value, &error)) << qPrintable(error);
  EXPECT_EQ(value.toString(), QString("3.17.2-studio.1+portable"));
}

TEST(SemVer, RejectsInvalidVersions)
{
  SemVer value;
  EXPECT_FALSE(SemVer::parse("", &value));
  EXPECT_FALSE(SemVer::parse("01.2.3", &value));
  EXPECT_FALSE(SemVer::parse("1.2", &value));
  EXPECT_FALSE(SemVer::parse("1.2.3-01", &value));
  EXPECT_FALSE(SemVer::parse("1.2.3-alpha..1", &value));
  EXPECT_FALSE(SemVer::parse("1.2.3+", &value));
  EXPECT_FALSE(SemVer::parse("1.2.3_beta", &value));
  EXPECT_FALSE(SemVer::parse("18446744073709551616.0.0", &value));
}

TEST(SemVer, ComparesAccordingToSemVerTwo)
{
  const char* ordered[] = { "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta",
                            "1.0.0-beta", "1.0.0-beta.2", "1.0.0-beta.11",
                            "1.0.0-rc.1", "1.0.0" };
  for (size_t index = 1; index < std::size(ordered); ++index)
  {
    EXPECT_LT(SemVer::compare(semver(ordered[index - 1]), semver(ordered[index])), 0);
  }
  EXPECT_EQ(SemVer::compare(semver("1.0.0+one"), semver("1.0.0+two")), 0);
}

TEST(UpdateManifest, ParsesStrictSchemaV1)
{
  UpdateManifest manifest;
  QString error;
  ASSERT_TRUE(UpdateManifest::parse(validManifest(), &manifest, &error)) << qPrintable(error);
  EXPECT_EQ(manifest.schemaVersion, 1);
  EXPECT_EQ(manifest.channel, QString("beta"));
  EXPECT_EQ(manifest.size, 123456);
  EXPECT_TRUE(manifest.matches("windows", "x86_64", "beta"));
  EXPECT_TRUE(manifest.isNewerThan("3.17.2-studio.1"));
  EXPECT_TRUE(manifest.supportsCurrentVersion("3.17.2-studio.1"));
}

TEST(UpdateManifest, RejectsUnknownOrMissingFields)
{
  UpdateManifest manifest;
  QByteArray unknown = validManifest();
  unknown.replace("\"schemaVersion\": 1,", "\"schemaVersion\": 1, \"extra\": true,");
  EXPECT_FALSE(UpdateManifest::parse(unknown, &manifest));

  QByteArray missing = validManifest();
  missing.replace("\"channel\": \"beta\",", "");
  EXPECT_FALSE(UpdateManifest::parse(missing, &manifest));

  QByteArray legacyUrl = validManifest();
  legacyUrl.replace("\"assetUrl\":", "\"url\":");
  EXPECT_FALSE(UpdateManifest::parse(legacyUrl, &manifest));
}

TEST(UpdateManifest, RejectsUnsafeUrlAndInvalidIntegrity)
{
  UpdateManifest manifest;
  QByteArray unsafe = validManifest();
  unsafe.replace("https://github.com/", "http://github.com/");
  EXPECT_FALSE(UpdateManifest::parse(unsafe, &manifest));

  QByteArray badHash = validManifest();
  badHash.replace(
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", "abcd");
  EXPECT_FALSE(UpdateManifest::parse(badHash, &manifest));

  QByteArray badSize = validManifest();
  badSize.replace("\"size\": 123456", "\"size\": -1");
  EXPECT_FALSE(UpdateManifest::parse(badSize, &manifest));

  QByteArray fractionalSize = validManifest();
  fractionalSize.replace("\"size\": 123456", "\"size\": 1.5");
  EXPECT_FALSE(UpdateManifest::parse(fractionalSize, &manifest));

  QByteArray wrongType = validManifest();
  wrongType.replace("\"size\": 123456", "\"size\": \"123456\"");
  EXPECT_FALSE(UpdateManifest::parse(wrongType, &manifest));
}

TEST(UpdateManifest, RejectsInvalidTimeAndMinimumVersion)
{
  UpdateManifest manifest;
  QByteArray badTime = validManifest();
  badTime.replace("2026-08-20T12:00:00Z", "not-a-time");
  EXPECT_FALSE(UpdateManifest::parse(badTime, &manifest));

  QByteArray badMinimum = validManifest();
  badMinimum.replace("\"minimumSupportedVersion\": \"3.17.0\"",
                     "\"minimumSupportedVersion\": \"4.0.0\"");
  EXPECT_FALSE(UpdateManifest::parse(badMinimum, &manifest));

  QByteArray lookalikeHost = validManifest();
  lookalikeHost.replace("https://github.com/", "https://github.com.example.invalid/");
  EXPECT_FALSE(UpdateManifest::parse(lookalikeHost, &manifest));

  QByteArray credentialUrl = validManifest();
  credentialUrl.replace("https://github.com/", "https://user@github.com/");
  EXPECT_FALSE(UpdateManifest::parse(credentialUrl, &manifest));

  QByteArray alternatePort = validManifest();
  alternatePort.replace("https://github.com/", "https://github.com:444/");
  EXPECT_FALSE(UpdateManifest::parse(alternatePort, &manifest));
}

TEST(UpdateManifest, RejectsUnsupportedPlatformArchitectureAndChannel)
{
  UpdateManifest manifest;
  QByteArray platform = validManifest();
  platform.replace("\"platform\": \"windows\"", "\"platform\": \"linux\"");
  EXPECT_FALSE(UpdateManifest::parse(platform, &manifest));

  QByteArray architecture = validManifest();
  architecture.replace("\"arch\": \"x86_64\"", "\"arch\": \"aarch64\"");
  EXPECT_FALSE(UpdateManifest::parse(architecture, &manifest));

  QByteArray channel = validManifest();
  channel.replace("\"channel\": \"beta\"", "\"channel\": \"nightly\"");
  EXPECT_FALSE(UpdateManifest::parse(channel, &manifest));
}

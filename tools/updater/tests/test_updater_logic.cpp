#include "updater_logic.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using StudioUpdater::Options;

namespace
{
QStringList validArguments(const QTemporaryDir& temporary)
{
  return { QStringLiteral("updater.exe"),
           QStringLiteral("--protocol"), QStringLiteral("1"),
           QStringLiteral("--package"), QDir::cleanPath(temporary.filePath("update.zip")),
           QStringLiteral("--install-root"), QDir::cleanPath(temporary.filePath("install")),
           QStringLiteral("--extract-script"), QDir::cleanPath(temporary.filePath("extract.ps1")),
           QStringLiteral("--version"), QStringLiteral("3.17.3-studio.1"),
           QStringLiteral("--sha256"),
           QString(64, QLatin1Char('a')),
           QStringLiteral("--size"), QStringLiteral("123"),
           QStringLiteral("--parent-pid"), QStringLiteral("42"),
           QStringLiteral("--health-timeout"), QStringLiteral("45") };
}

void writeFile(const QString& path, const QByteArray& contents = QByteArray("x"))
{
  ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(contents), contents.size());
}

void createStagedPackage(const QString& root, const QString& version)
{
  const QByteArray marker =
      QStringLiteral(
          R"({"schemaVersion":1,"product":"RosPlotJugglerStudio","version":"%1","platform":"windows-x86_64","entrypoint":"bin/RosPlotJugglerStudio.exe","files":[]})")
          .arg(version)
          .toUtf8();
  writeFile(QDir(root).filePath(QStringLiteral("manifest.json")), marker);
  writeFile(QDir(root).filePath(QStringLiteral("bin/RosPlotJugglerStudio.exe")));
  writeFile(QDir(root).filePath(QStringLiteral("bin/updater/RosPlotJugglerUpdater.exe")));
  writeFile(QDir(root).filePath(QStringLiteral("bin/updater/extract-update.ps1")));
}
}  // namespace

TEST(UpdaterArguments, ParsesVersionedProtocol)
{
  QTemporaryDir temporary;
  Options options;
  QString error;
  ASSERT_TRUE(StudioUpdater::parseOptions(validArguments(temporary), &options, &error))
      << qPrintable(error);
  EXPECT_EQ(options.protocol, StudioUpdater::ProtocolVersion);
  EXPECT_EQ(options.expectedSize, 123);
}

TEST(UpdaterArguments, RejectsRelativeUnknownAndDuplicateArguments)
{
  QTemporaryDir temporary;
  Options options;
  QStringList relative = validArguments(temporary);
  relative[4] = QStringLiteral("relative.zip");
  EXPECT_FALSE(StudioUpdater::parseOptions(relative, &options));

  QStringList duplicate = validArguments(temporary);
  duplicate << QStringLiteral("--size") << QStringLiteral("456");
  EXPECT_FALSE(StudioUpdater::parseOptions(duplicate, &options));
}

TEST(UpdaterArguments, RejectsInvalidProtocolHashSizePidTimeoutAndVersion)
{
  QTemporaryDir temporary;
  Options options;
  const QList<QPair<int, QString>> invalid = {
    { 2, QStringLiteral("2") },    { 10, QStringLiteral("") },
    { 12, QStringLiteral("xyz") }, { 14, QStringLiteral("0") },
    { 16, QStringLiteral("0") },   { 18, QStringLiteral("4") }
  };
  for (const auto& replacement : invalid)
  {
    QStringList arguments = validArguments(temporary);
    arguments[replacement.first] = replacement.second;
    EXPECT_FALSE(StudioUpdater::parseOptions(arguments, &options));
  }
}

TEST(UpdaterPaths, RejectsTraversalAndAbsoluteArchiveEntries)
{
  QTemporaryDir temporary;
  EXPECT_TRUE(StudioUpdater::isSafeArchiveEntry(temporary.path(), "root/bin/app.exe"));
  EXPECT_FALSE(StudioUpdater::isSafeArchiveEntry(temporary.path(), "../outside.exe"));
  EXPECT_FALSE(StudioUpdater::isSafeArchiveEntry(temporary.path(), R"(dir\..\..\outside.exe)"));
  EXPECT_FALSE(StudioUpdater::isSafeArchiveEntry(temporary.path(), "C:/Windows/system.ini"));
  EXPECT_FALSE(StudioUpdater::isSafeArchiveEntry(temporary.path(), "/absolute"));
  EXPECT_FALSE(StudioUpdater::isSafeArchiveEntry(temporary.path(), R"(\\server\share\file)"));
}

TEST(UpdaterIntegrity, VerifiesSizeAndSha256)
{
  QTemporaryDir temporary;
  const QString path = temporary.filePath(QStringLiteral("package.zip"));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  const QByteArray contents("verified update");
  ASSERT_EQ(file.write(contents), contents.size());
  file.close();
  const QByteArray hash = QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex();
  EXPECT_TRUE(StudioUpdater::verifyFile(path, contents.size(), hash));
  EXPECT_FALSE(StudioUpdater::verifyFile(path, contents.size() + 1, hash));
  EXPECT_FALSE(StudioUpdater::verifyFile(path, contents.size(), QByteArray(64, '0')));
}

TEST(UpdaterPermissions, SimulatesUnwritableParentWithNonDirectory)
{
  QTemporaryDir temporary;
  const QString filePath = temporary.filePath(QStringLiteral("not-a-directory"));
  writeFile(filePath);
  QString error;
  EXPECT_TRUE(StudioUpdater::checkDirectoryWritable(temporary.path(), &error)) << qPrintable(error);
  EXPECT_FALSE(StudioUpdater::checkDirectoryWritable(filePath, &error));
}

TEST(UpdaterStaging, ValidatesMarkerVersionAndRequiredFiles)
{
  QTemporaryDir temporary;
  const QString staged = temporary.filePath(QStringLiteral("package"));
  createStagedPackage(staged, QStringLiteral("3.18.0"));
  QString error;
  EXPECT_TRUE(StudioUpdater::validateStagedPackage(staged, QStringLiteral("3.18.0"), &error))
      << qPrintable(error);
  EXPECT_FALSE(StudioUpdater::validateStagedPackage(staged, QStringLiteral("3.18.1"), &error));

  ASSERT_TRUE(QFile::remove(
      QDir(staged).filePath(QStringLiteral("bin/updater/extract-update.ps1"))));
  EXPECT_FALSE(StudioUpdater::validateStagedPackage(staged, QStringLiteral("3.18.0"), &error));
}

TEST(UpdaterStaging, RejectsInvalidMarker)
{
  QTemporaryDir temporary;
  const QString staged = temporary.filePath(QStringLiteral("package"));
  createStagedPackage(staged, QStringLiteral("3.18.0"));
  writeFile(QDir(staged).filePath(QStringLiteral("manifest.json")), QByteArray("{}"));
  EXPECT_FALSE(StudioUpdater::validateStagedPackage(staged, QStringLiteral("3.18.0")));
}

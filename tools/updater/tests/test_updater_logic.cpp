#include "updater_logic.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <chrono>
#include <thread>

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

TEST(UpdaterWorkDirectories, UsesShortNamesNextToTheInstallRoot)
{
  using StudioUpdater::WorkDirectory;
  const QString root = QStringLiteral("C:/Tools/RosPlotJugglerStudio");
  const QString token = StudioUpdater::newWorkDirectoryToken();
  ASSERT_EQ(token.size(), 8);
  const QString staging = StudioUpdater::workDirectoryPath(root, WorkDirectory::Staging, token);
  EXPECT_EQ(staging, QStringLiteral("C:/Tools/.RosPlotJugglerStudio-new-") + token);
  EXPECT_EQ(QFileInfo(staging).fileName().size(), 34);
  EXPECT_EQ(StudioUpdater::workDirectoryPath(root, WorkDirectory::Backup, token),
            QStringLiteral("C:/Tools/.RosPlotJugglerStudio-old-") + token);
  EXPECT_EQ(StudioUpdater::workDirectoryPath(root, WorkDirectory::Failed, token),
            QStringLiteral("C:/Tools/.RosPlotJugglerStudio-bad-") + token);
  for (const WorkDirectory kind :
       { WorkDirectory::Staging, WorkDirectory::Backup, WorkDirectory::Failed })
  {
    EXPECT_TRUE(StudioUpdater::isWorkDirectoryName(
        QStringLiteral("RosPlotJugglerStudio"),
        QFileInfo(StudioUpdater::workDirectoryPath(root, kind, token)).fileName()));
  }
}

TEST(UpdaterWorkDirectories, RecognizesOnlyLeftoversOfTheSameInstall)
{
  const QString root = QStringLiteral("RosPlotJugglerStudio");
  const QString uuid = QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e");
  EXPECT_TRUE(StudioUpdater::isWorkDirectoryName(root, QStringLiteral(".RosPlotJugglerStudio-backup-") + uuid));
  EXPECT_TRUE(StudioUpdater::isWorkDirectoryName(root, QStringLiteral(".RosPlotJugglerStudio-staging-") + uuid));
  EXPECT_TRUE(StudioUpdater::isWorkDirectoryName(root, QStringLiteral("RosPlotJugglerStudio.failed-") + uuid));
  EXPECT_TRUE(StudioUpdater::isWorkDirectoryName(root, QStringLiteral(".RosPlotJugglerStudio-old-1234abcd")));

  EXPECT_FALSE(StudioUpdater::isWorkDirectoryName(root, root));
  EXPECT_FALSE(StudioUpdater::isWorkDirectoryName(root, QStringLiteral(".RosPlotJugglerStudio-old-1234abc")));
  EXPECT_FALSE(StudioUpdater::isWorkDirectoryName(root, QStringLiteral(".RosPlotJugglerStudio-old-1234abcde")));
  EXPECT_FALSE(StudioUpdater::isWorkDirectoryName(root, QStringLiteral(".RosPlotJugglerStudio-backup-not-a-uuid")));
  EXPECT_FALSE(StudioUpdater::isWorkDirectoryName(root, QStringLiteral(".OtherApp-old-1234abcd")));
  EXPECT_FALSE(StudioUpdater::isWorkDirectoryName(QStringLiteral("Studio"),
                                                  QStringLiteral(".RosPlotJugglerStudio-old-1234abcd")));
  EXPECT_FALSE(StudioUpdater::isWorkDirectoryName(QStringLiteral("a.b"), QStringLiteral(".aXb-old-1234abcd")));
  EXPECT_FALSE(StudioUpdater::isWorkDirectoryName(QString(), QStringLiteral(".-old-1234abcd")));
}

TEST(UpdaterWorkDirectories, RemovesLeftoversButKeepsInstallAndUnrelatedFolders)
{
  QTemporaryDir temporary;
  const QString root = temporary.filePath(QStringLiteral("RosPlotJugglerStudio"));
  createStagedPackage(root, QStringLiteral("3.17.10"));
  const QString legacy =
      temporary.filePath(QStringLiteral(".RosPlotJugglerStudio-backup-0f8fad5b-d9cb-469f-a165-70867728950e"));
  const QString current = StudioUpdater::workDirectoryPath(
      root, StudioUpdater::WorkDirectory::Backup, QStringLiteral("1234abcd"));
  const QString unrelated = temporary.filePath(QStringLiteral(".OtherApp-old-1234abcd"));
  const QString sibling = temporary.filePath(QStringLiteral("RosPlotJugglerStudio-3.17.8"));
  writeFile(QDir(legacy).filePath(QStringLiteral("bin/libstdc++-6.dll")));
  writeFile(QDir(current).filePath(QStringLiteral("bin/Qt5Core.dll")));
  writeFile(QDir(unrelated).filePath(QStringLiteral("keep.txt")));
  writeFile(QDir(sibling).filePath(QStringLiteral("keep.txt")));

  EXPECT_EQ(StudioUpdater::removeLeftoverWorkDirectories(root), 2);
  EXPECT_FALSE(QFileInfo::exists(legacy));
  EXPECT_FALSE(QFileInfo::exists(current));
  EXPECT_TRUE(QFileInfo::exists(QDir(root).filePath(QStringLiteral("manifest.json"))));
  EXPECT_TRUE(QFileInfo::exists(QDir(unrelated).filePath(QStringLiteral("keep.txt"))));
  EXPECT_TRUE(QFileInfo::exists(QDir(sibling).filePath(QStringLiteral("keep.txt"))));
}

TEST(UpdaterRename, RetriesWhileAFileInsideIsOpen)
{
  QTemporaryDir temporary;
  const QString from = temporary.filePath(QStringLiteral("from"));
  const QString to = temporary.filePath(QStringLiteral("to"));
  writeFile(QDir(from).filePath(QStringLiteral("bin/held.dll")));
  QFile held(QDir(from).filePath(QStringLiteral("bin/held.dll")));
  ASSERT_TRUE(held.open(QIODevice::ReadOnly));
  std::thread release([&held]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    held.close();
  });
  int attempts = 0;
  const bool renamed = StudioUpdater::renameDirectory(from, to, 10000, &attempts);
  release.join();
  EXPECT_TRUE(renamed);
  EXPECT_GT(attempts, 1);
  EXPECT_TRUE(QFileInfo::exists(QDir(to).filePath(QStringLiteral("bin/held.dll"))));
}

TEST(UpdaterRename, DoesNotRetryOntoAnExistingDirectory)
{
  QTemporaryDir temporary;
  const QString from = temporary.filePath(QStringLiteral("from"));
  const QString to = temporary.filePath(QStringLiteral("to"));
  writeFile(QDir(from).filePath(QStringLiteral("a.txt")));
  writeFile(QDir(to).filePath(QStringLiteral("b.txt")));
  int attempts = 0;
  EXPECT_FALSE(StudioUpdater::renameDirectory(from, to, 10000, &attempts));
  EXPECT_EQ(attempts, 1);
}

TEST(UpdaterRuntime, CopiesListedDllsNextToTheUpdater)
{
  QTemporaryDir temporary;
  const QString bin = temporary.filePath(QStringLiteral("bin"));
  const QString updater = QDir(bin).filePath(QStringLiteral("updater"));
  const QString destination = temporary.filePath(QStringLiteral("run"));
  writeFile(QDir(updater).filePath(QStringLiteral("RosPlotJugglerUpdater.exe")));
  writeFile(QDir(updater).filePath(QStringLiteral("extract-update.ps1")));
  writeFile(QDir(bin).filePath(QStringLiteral("Qt5Core.dll")));
  writeFile(QDir(bin).filePath(QStringLiteral("libstdc++-6.dll")));
  writeFile(QDir(updater).filePath(QStringLiteral("runtime-dlls.txt")),
            QByteArray("Qt5Core.dll\r\nlibstdc++-6.dll\r\n\r\n"));
  ASSERT_TRUE(QDir().mkpath(destination));

  QString error;
  ASSERT_TRUE(StudioUpdater::stageUpdaterRuntime(updater, bin, destination, &error))
      << qPrintable(error);
  for (const char* name : { "RosPlotJugglerUpdater.exe", "extract-update.ps1", "Qt5Core.dll",
                            "libstdc++-6.dll" })
  {
    EXPECT_TRUE(QFileInfo::exists(QDir(destination).filePath(QString::fromLatin1(name)))) << name;
  }
}

TEST(UpdaterRuntime, RejectsEntriesThatAreNotPackagedDllNames)
{
  QTemporaryDir temporary;
  const QString bin = temporary.filePath(QStringLiteral("bin"));
  const QString updater = QDir(bin).filePath(QStringLiteral("updater"));
  writeFile(QDir(updater).filePath(QStringLiteral("RosPlotJugglerUpdater.exe")));
  writeFile(QDir(updater).filePath(QStringLiteral("extract-update.ps1")));
  writeFile(temporary.filePath(QStringLiteral("secret.dll")));

  for (const QByteArray& list : { QByteArray("..\\secret.dll\n"), QByteArray("missing.dll\n") })
  {
    writeFile(QDir(updater).filePath(QStringLiteral("runtime-dlls.txt")), list);
    const QString destination = temporary.filePath(QStringLiteral("run-") + QString::fromLatin1(list.left(3).toHex()));
    ASSERT_TRUE(QDir().mkpath(destination));
    EXPECT_FALSE(StudioUpdater::stageUpdaterRuntime(updater, bin, destination)) << list.constData();
    EXPECT_FALSE(QFileInfo::exists(QDir(destination).filePath(QStringLiteral("secret.dll"))));
  }
}

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace StudioUpdater
{

constexpr int ProtocolVersion = 1;

struct Options
{
  int protocol = 0;
  QString packagePath;
  QString installRoot;
  QString extractionScript;
  QString expectedVersion;
  QByteArray expectedSha256;
  qint64 expectedSize = 0;
  quint64 parentPid = 0;
  int healthTimeoutSeconds = 45;
};

bool parseOptions(const QStringList& arguments, Options* options, QString* error = nullptr);
bool validateAbsolutePath(const QString& path, QString* error = nullptr);
bool verifyFile(const QString& path, qint64 expectedSize, const QByteArray& expectedSha256,
                QString* error = nullptr);
bool isSafeArchiveEntry(const QString& destinationRoot, const QString& entryName,
                        QString* error = nullptr);
bool validateInstallMarker(const QString& installRoot, QString* version = nullptr,
                           QString* error = nullptr);
bool validateStagedPackage(const QString& stagedRoot, const QString& expectedVersion,
                           QString* error = nullptr);
bool checkDirectoryWritable(const QString& directory, QString* error = nullptr);
quint64 directorySize(const QString& root);

enum class WorkDirectory
{
  Staging,
  Backup,
  Failed
};

QString newWorkDirectoryToken();
// The package is extracted below the staging directory by Windows PowerShell, which cannot
// open paths longer than MAX_PATH, so these names are kept short.
QString workDirectoryPath(const QString& installRoot, WorkDirectory kind, const QString& token);
// Also matches the longer names that updaters before 3.17.10 used.
bool isWorkDirectoryName(const QString& rootName, const QString& name);
int removeLeftoverWorkDirectories(const QString& installRoot);

// Security scanners briefly keep freshly written files open, and Windows refuses to rename
// a directory while any file inside it is open.
bool renameDirectory(const QString& from, const QString& to, int timeoutMs = 30000,
                     int* attempts = nullptr);

// Copies the updater, its extraction script and the DLLs listed in runtime-dlls.txt, so
// the updater does not run on DLLs of the installation it replaces.
bool stageUpdaterRuntime(const QString& updaterDirectory, const QString& binDirectory,
                         const QString& destination, QString* error = nullptr);

}  // namespace StudioUpdater

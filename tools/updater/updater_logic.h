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

}  // namespace StudioUpdater

#include "updater_logic.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

namespace StudioUpdater
{
namespace
{
void setError(QString* error, const QString& text)
{
  if (error)
  {
    *error = text;
  }
}

bool readMarker(const QString& path, QJsonObject* object, QString* error)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
  {
    setError(error, QStringLiteral("Unable to read package marker: %1").arg(path));
    return false;
  }
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
  {
    setError(error, QStringLiteral("Invalid package marker JSON"));
    return false;
  }
  *object = document.object();
  return true;
}

bool markerIdentityIsValid(const QJsonObject& object, QString* error)
{
  if (object.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
      object.value(QStringLiteral("product")).toString() !=
          QStringLiteral("RosPlotJugglerStudio") ||
      object.value(QStringLiteral("version")).toString().isEmpty() ||
      object.value(QStringLiteral("platform")).toString() != QStringLiteral("windows-x86_64") ||
      object.value(QStringLiteral("entrypoint")).toString() !=
          QStringLiteral("bin/RosPlotJugglerStudio.exe"))
  {
    setError(error, QStringLiteral("Package marker identity or schema is invalid"));
    return false;
  }
  return true;
}
}  // namespace

bool validateAbsolutePath(const QString& path, QString* error)
{
  const QFileInfo info(path);
  if (path.isEmpty() || !info.isAbsolute() || QDir::isRelativePath(path) ||
      QDir::cleanPath(path) != path)
  {
    setError(error, QStringLiteral("Path must be absolute and normalized: %1").arg(path));
    return false;
  }
  return true;
}

bool parseOptions(const QStringList& arguments, Options* options, QString* error)
{
  if (!options)
  {
    setError(error, QStringLiteral("Options output is null"));
    return false;
  }
  QHash<QString, QString> values;
  for (int index = 1; index < arguments.size(); index += 2)
  {
    if (index + 1 >= arguments.size() || !arguments.at(index).startsWith(QStringLiteral("--")) ||
        values.contains(arguments.at(index)))
    {
      setError(error, QStringLiteral("Malformed or duplicate updater argument"));
      return false;
    }
    values.insert(arguments.at(index), arguments.at(index + 1));
  }
  const QSet<QString> required = {
    QStringLiteral("--protocol"),       QStringLiteral("--package"),
    QStringLiteral("--install-root"),   QStringLiteral("--extract-script"),
    QStringLiteral("--version"),        QStringLiteral("--sha256"),
    QStringLiteral("--size"),           QStringLiteral("--parent-pid"),
    QStringLiteral("--health-timeout")
  };
  const QSet<QString> actual(values.keyBegin(), values.keyEnd());
  if (actual != required)
  {
    setError(error, QStringLiteral("Updater arguments are missing or unknown"));
    return false;
  }

  bool protocolOk = false;
  bool sizeOk = false;
  bool pidOk = false;
  bool timeoutOk = false;
  Options parsed;
  parsed.protocol = values.value(QStringLiteral("--protocol")).toInt(&protocolOk);
  parsed.expectedSize = values.value(QStringLiteral("--size")).toLongLong(&sizeOk);
  parsed.parentPid = values.value(QStringLiteral("--parent-pid")).toULongLong(&pidOk);
  parsed.healthTimeoutSeconds =
      values.value(QStringLiteral("--health-timeout")).toInt(&timeoutOk);
  parsed.packagePath = QDir::cleanPath(values.value(QStringLiteral("--package")));
  parsed.installRoot = QDir::cleanPath(values.value(QStringLiteral("--install-root")));
  parsed.extractionScript = QDir::cleanPath(values.value(QStringLiteral("--extract-script")));
  parsed.expectedVersion = values.value(QStringLiteral("--version"));
  parsed.expectedSha256 = values.value(QStringLiteral("--sha256")).toLatin1().toLower();

  static const QRegularExpression hashExpression(QStringLiteral("^[0-9a-f]{64}$"));
  if (!protocolOk || parsed.protocol != ProtocolVersion || !sizeOk || parsed.expectedSize < 1 ||
      !pidOk || parsed.parentPid == 0 || !timeoutOk || parsed.healthTimeoutSeconds < 5 ||
      parsed.healthTimeoutSeconds > 300 || parsed.expectedVersion.isEmpty() ||
      !hashExpression.match(QString::fromLatin1(parsed.expectedSha256)).hasMatch() ||
      !validateAbsolutePath(parsed.packagePath, error) ||
      !validateAbsolutePath(parsed.installRoot, error) ||
      !validateAbsolutePath(parsed.extractionScript, error))
  {
    if (error && error->isEmpty())
    {
      *error = QStringLiteral("Updater argument value is invalid");
    }
    return false;
  }
  *options = parsed;
  return true;
}

bool verifyFile(const QString& path, qint64 expectedSize, const QByteArray& expectedSha256,
                QString* error)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() != expectedSize)
  {
    setError(error, QStringLiteral("Update package size does not match manifest"));
    return false;
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!file.atEnd())
  {
    const QByteArray chunk = file.read(1024 * 1024);
    if (chunk.isEmpty() && file.error() != QFile::NoError)
    {
      setError(error, QStringLiteral("Unable to read update package"));
      return false;
    }
    hash.addData(chunk);
  }
  if (hash.result().toHex().toLower() != expectedSha256.toLower())
  {
    setError(error, QStringLiteral("Update package SHA-256 does not match manifest"));
    return false;
  }
  return true;
}

bool isSafeArchiveEntry(const QString& destinationRoot, const QString& entryName, QString* error)
{
  const QString portable = entryName;
  if (portable.isEmpty() || portable.startsWith(QLatin1Char('/')) ||
      portable.startsWith(QLatin1Char('\\')) || portable.contains(QLatin1Char(':')))
  {
    setError(error, QStringLiteral("Archive entry uses an absolute path"));
    return false;
  }
  const QString root =
      QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(destinationRoot).absoluteFilePath()));
  const QString target = QDir::fromNativeSeparators(
      QDir::cleanPath(QFileInfo(QDir(root).filePath(portable)).absoluteFilePath()));
  const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
  if (!target.startsWith(prefix, Qt::CaseInsensitive))
  {
    setError(error, QStringLiteral("Archive entry escapes the destination"));
    return false;
  }
  return true;
}

bool validateInstallMarker(const QString& installRoot, QString* version, QString* error)
{
  QJsonObject marker;
  if (!readMarker(QDir(installRoot).filePath(QStringLiteral("manifest.json")), &marker, error) ||
      !markerIdentityIsValid(marker, error))
  {
    return false;
  }
  if (version)
  {
    *version = marker.value(QStringLiteral("version")).toString();
  }
  return true;
}

bool validateStagedPackage(const QString& stagedRoot, const QString& expectedVersion,
                           QString* error)
{
  QJsonObject marker;
  if (!readMarker(QDir(stagedRoot).filePath(QStringLiteral("manifest.json")), &marker, error) ||
      !markerIdentityIsValid(marker, error) ||
      marker.value(QStringLiteral("version")).toString() != expectedVersion ||
      !marker.value(QStringLiteral("files")).isArray())
  {
    if (error && error->isEmpty())
    {
      *error = QStringLiteral("Staged package version or file manifest is invalid");
    }
    return false;
  }
  const QStringList required = {
    QStringLiteral("bin/RosPlotJugglerStudio.exe"),
    QStringLiteral("bin/updater/RosPlotJugglerUpdater.exe"),
    QStringLiteral("bin/updater/extract-update.ps1")
  };
  for (const QString& relative : required)
  {
    const QFileInfo file(QDir(stagedRoot).filePath(relative));
    if (!file.isFile() || file.isSymLink())
    {
      setError(error, QStringLiteral("Staged package is missing required file: %1").arg(relative));
      return false;
    }
  }
  return true;
}

bool checkDirectoryWritable(const QString& directory, QString* error)
{
  if (!QFileInfo(directory).isDir())
  {
    setError(error, QStringLiteral("Install directory parent is not a directory"));
    return false;
  }
  const QString probe = QDir(directory).filePath(
      QStringLiteral(".rspj-write-probe-%1").arg(QUuid::createUuid().toString()));
  QFile file(probe);
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
  {
    setError(error, QStringLiteral("Install directory parent is not writable"));
    return false;
  }
  file.close();
  if (!QFile::remove(probe))
  {
    setError(error, QStringLiteral("Install directory write probe could not be removed"));
    return false;
  }
  return true;
}

quint64 directorySize(const QString& root)
{
  quint64 total = 0;
  QDirIterator iterator(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
  while (iterator.hasNext())
  {
    iterator.next();
    total += static_cast<quint64>(iterator.fileInfo().size());
  }
  return total;
}

}  // namespace StudioUpdater

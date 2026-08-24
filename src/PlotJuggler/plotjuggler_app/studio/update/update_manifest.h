#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QUrl>

namespace StudioUpdate
{

class SemVer
{
public:
  static bool parse(const QString& text, SemVer* result, QString* error = nullptr);
  static int compare(const SemVer& lhs, const SemVer& rhs);

  QString toString() const;

  quint64 major = 0;
  quint64 minor = 0;
  quint64 patch = 0;
  QStringList prerelease;
  QString buildMetadata;
};

struct UpdateManifest
{
  static bool parse(const QByteArray& json, UpdateManifest* result, QString* error = nullptr);
  static bool isAllowedHttpsUrl(const QUrl& url, QString* error = nullptr);

  bool isNewerThan(const QString& currentVersion) const;
  bool supportsCurrentVersion(const QString& currentVersion) const;
  bool matches(const QString& expectedPlatform, const QString& expectedArch,
               const QString& expectedChannel) const;

  int schemaVersion = 0;
  SemVer version;
  QString platform;
  QString arch;
  QString channel;
  QDateTime publishedAt;
  SemVer minimumSupportedVersion;
  QUrl assetUrl;
  QUrl releaseNotesUrl;
  qint64 size = 0;
  QByteArray sha256;
};

QString currentPlatform();
QString currentArchitecture();

}  // namespace StudioUpdate

#include "update_manifest.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSysInfo>
#include <algorithm>
#include <cmath>
#include <limits>

namespace StudioUpdate
{
namespace
{
void setError(QString* error, const QString& message)
{
  if (error)
  {
    *error = message;
  }
}

bool parseNumericIdentifier(const QString& value, quint64* number)
{
  if (value.size() > 1 && value.startsWith(QLatin1Char('0')))
  {
    return false;
  }
  bool ok = false;
  const auto parsed = value.toULongLong(&ok);
  if (ok)
  {
    *number = parsed;
  }
  return ok;
}
}  // namespace

bool SemVer::parse(const QString& text, SemVer* result, QString* error)
{
  static const QRegularExpression expression(
      QStringLiteral("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)"
                     "(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?"
                     "(?:\\+([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?$"));
  const auto match = expression.match(text);
  if (!match.hasMatch() || !result)
  {
    setError(error, QStringLiteral("Invalid SemVer: %1").arg(text));
    return false;
  }

  SemVer parsed;
  if (!parseNumericIdentifier(match.captured(1), &parsed.major) ||
      !parseNumericIdentifier(match.captured(2), &parsed.minor) ||
      !parseNumericIdentifier(match.captured(3), &parsed.patch))
  {
    setError(error, QStringLiteral("Invalid numeric SemVer component"));
    return false;
  }
  if (!match.captured(4).isEmpty())
  {
    parsed.prerelease = match.captured(4).split(QLatin1Char('.'));
    for (const auto& identifier : parsed.prerelease)
    {
      if (identifier.size() > 1 && identifier.at(0) == QLatin1Char('0') &&
          identifier.at(0).isDigit() &&
          std::all_of(identifier.cbegin(), identifier.cend(),
                      [](QChar ch) { return ch.isDigit(); }))
      {
        setError(error, QStringLiteral("Numeric prerelease identifiers may not have leading zeroes"));
        return false;
      }
    }
  }
  parsed.buildMetadata = match.captured(5);
  *result = parsed;
  return true;
}

int SemVer::compare(const SemVer& lhs, const SemVer& rhs)
{
  const quint64 left[] = { lhs.major, lhs.minor, lhs.patch };
  const quint64 right[] = { rhs.major, rhs.minor, rhs.patch };
  for (int index = 0; index < 3; ++index)
  {
    if (left[index] != right[index])
    {
      return left[index] < right[index] ? -1 : 1;
    }
  }
  if (lhs.prerelease.isEmpty() || rhs.prerelease.isEmpty())
  {
    return lhs.prerelease.isEmpty() == rhs.prerelease.isEmpty()
               ? 0
               : (lhs.prerelease.isEmpty() ? 1 : -1);
  }
  const int common = qMin(lhs.prerelease.size(), rhs.prerelease.size());
  for (int index = 0; index < common; ++index)
  {
    const QString& leftId = lhs.prerelease.at(index);
    const QString& rightId = rhs.prerelease.at(index);
    bool leftNumeric = false;
    bool rightNumeric = false;
    const quint64 leftNumber = leftId.toULongLong(&leftNumeric);
    const quint64 rightNumber = rightId.toULongLong(&rightNumeric);
    if (leftNumeric && rightNumeric && leftNumber != rightNumber)
    {
      return leftNumber < rightNumber ? -1 : 1;
    }
    if (leftNumeric != rightNumeric)
    {
      return leftNumeric ? -1 : 1;
    }
    const int lexical = QString::compare(leftId, rightId, Qt::CaseSensitive);
    if (lexical != 0)
    {
      return lexical < 0 ? -1 : 1;
    }
  }
  if (lhs.prerelease.size() == rhs.prerelease.size())
  {
    return 0;
  }
  return lhs.prerelease.size() < rhs.prerelease.size() ? -1 : 1;
}

QString SemVer::toString() const
{
  QString value = QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
  if (!prerelease.isEmpty())
  {
    value += QLatin1Char('-') + prerelease.join(QLatin1Char('.'));
  }
  if (!buildMetadata.isEmpty())
  {
    value += QLatin1Char('+') + buildMetadata;
  }
  return value;
}

bool UpdateManifest::isAllowedHttpsUrl(const QUrl& url, QString* error)
{
  static const QSet<QString> allowedHosts = {
    QStringLiteral("github.com"), QStringLiteral("objects.githubusercontent.com"),
    QStringLiteral("release-assets.githubusercontent.com")
  };
  if (!url.isValid() || url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0 ||
      !url.userInfo().isEmpty() || (url.port(-1) != -1 && url.port(-1) != 443) ||
      url.path().isEmpty() || !allowedHosts.contains(url.host().toLower()))
  {
    setError(error, QStringLiteral("URL must use HTTPS and an allowed GitHub host"));
    return false;
  }
  return true;
}

bool UpdateManifest::parse(const QByteArray& json, UpdateManifest* result, QString* error)
{
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject() || !result)
  {
    setError(error, QStringLiteral("Invalid manifest JSON: %1").arg(parseError.errorString()));
    return false;
  }

  const QJsonObject object = document.object();
  const QSet<QString> required = {
    QStringLiteral("schemaVersion"), QStringLiteral("version"), QStringLiteral("platform"),
    QStringLiteral("arch"),          QStringLiteral("channel"), QStringLiteral("publishedAt"),
    QStringLiteral("minimumSupportedVersion"), QStringLiteral("assetUrl"),
    QStringLiteral("releaseNotesUrl"), QStringLiteral("size"), QStringLiteral("sha256")
  };
  const QStringList keys = object.keys();
  const QSet<QString> actual(keys.cbegin(), keys.cend());
  if (actual != required)
  {
    setError(error, QStringLiteral("Manifest schema has missing or unknown fields"));
    return false;
  }
  if (!object.value(QStringLiteral("schemaVersion")).isDouble() ||
      object.value(QStringLiteral("schemaVersion")).toDouble() != 1.0 ||
      !object.value(QStringLiteral("version")).isString() ||
      !object.value(QStringLiteral("platform")).isString() ||
      !object.value(QStringLiteral("arch")).isString() ||
      !object.value(QStringLiteral("channel")).isString() ||
      !object.value(QStringLiteral("publishedAt")).isString() ||
      !object.value(QStringLiteral("minimumSupportedVersion")).isString() ||
      !object.value(QStringLiteral("assetUrl")).isString() ||
      !object.value(QStringLiteral("releaseNotesUrl")).isString() ||
      !object.value(QStringLiteral("size")).isDouble() ||
      !object.value(QStringLiteral("sha256")).isString())
  {
    setError(error, QStringLiteral("Manifest field types do not match schema v1"));
    return false;
  }

  UpdateManifest parsed;
  parsed.schemaVersion = 1;
  if (!SemVer::parse(object.value(QStringLiteral("version")).toString(), &parsed.version, error) ||
      !SemVer::parse(object.value(QStringLiteral("minimumSupportedVersion")).toString(),
                     &parsed.minimumSupportedVersion, error))
  {
    return false;
  }
  if (SemVer::compare(parsed.minimumSupportedVersion, parsed.version) > 0)
  {
    setError(error, QStringLiteral("minimumSupportedVersion exceeds update version"));
    return false;
  }

  parsed.platform = object.value(QStringLiteral("platform")).toString();
  parsed.arch = object.value(QStringLiteral("arch")).toString();
  parsed.channel = object.value(QStringLiteral("channel")).toString().toLower();
  if (parsed.platform.compare(QStringLiteral("windows"), Qt::CaseInsensitive) != 0 ||
      parsed.arch.compare(QStringLiteral("x86_64"), Qt::CaseInsensitive) != 0 ||
      (parsed.channel != QStringLiteral("stable") && parsed.channel != QStringLiteral("beta")))
  {
    setError(error, QStringLiteral("Invalid platform, architecture, or channel"));
    return false;
  }

  parsed.publishedAt =
      QDateTime::fromString(object.value(QStringLiteral("publishedAt")).toString(), Qt::ISODate);
  if (!parsed.publishedAt.isValid() || parsed.publishedAt.timeSpec() == Qt::LocalTime ||
      parsed.publishedAt > QDateTime::currentDateTimeUtc().addSecs(24 * 60 * 60))
  {
    setError(error, QStringLiteral("publishedAt must be a valid non-future ISO-8601 timestamp"));
    return false;
  }

  const double sizeValue = object.value(QStringLiteral("size")).toDouble();
  if (sizeValue < 1 || sizeValue > static_cast<double>(std::numeric_limits<qint64>::max()) ||
      std::floor(sizeValue) != sizeValue)
  {
    setError(error, QStringLiteral("Invalid update size"));
    return false;
  }
  parsed.size = static_cast<qint64>(sizeValue);
  parsed.sha256 = object.value(QStringLiteral("sha256")).toString().toLatin1().toLower();
  static const QRegularExpression hashExpression(QStringLiteral("^[0-9a-f]{64}$"));
  if (!hashExpression.match(QString::fromLatin1(parsed.sha256)).hasMatch())
  {
    setError(error, QStringLiteral("Invalid SHA-256 digest"));
    return false;
  }
  parsed.assetUrl = QUrl(object.value(QStringLiteral("assetUrl")).toString());
  parsed.releaseNotesUrl = QUrl(object.value(QStringLiteral("releaseNotesUrl")).toString());
  if (!isAllowedHttpsUrl(parsed.assetUrl, error) ||
      !isAllowedHttpsUrl(parsed.releaseNotesUrl, error))
  {
    return false;
  }
  *result = parsed;
  return true;
}

bool UpdateManifest::isNewerThan(const QString& currentVersion) const
{
  SemVer current;
  return SemVer::parse(currentVersion, &current) && SemVer::compare(version, current) > 0;
}

bool UpdateManifest::supportsCurrentVersion(const QString& currentVersion) const
{
  SemVer current;
  return SemVer::parse(currentVersion, &current) &&
         SemVer::compare(current, minimumSupportedVersion) >= 0;
}

bool UpdateManifest::matches(const QString& expectedPlatform, const QString& expectedArch,
                             const QString& expectedChannel) const
{
  return platform.compare(expectedPlatform, Qt::CaseInsensitive) == 0 &&
         arch.compare(expectedArch, Qt::CaseInsensitive) == 0 &&
         channel.compare(expectedChannel, Qt::CaseInsensitive) == 0;
}

QString currentPlatform()
{
#if defined(Q_OS_WIN)
  return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
  return QStringLiteral("macos");
#else
  return QStringLiteral("linux");
#endif
}

QString currentArchitecture()
{
  QString arch = QSysInfo::currentCpuArchitecture().toLower();
  if (arch == QStringLiteral("amd64"))
  {
    arch = QStringLiteral("x86_64");
  }
  else if (arch == QStringLiteral("arm64"))
  {
    arch = QStringLiteral("aarch64");
  }
  return arch;
}

}  // namespace StudioUpdate

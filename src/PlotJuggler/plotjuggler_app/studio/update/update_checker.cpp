#include "update_checker.h"

#include <QCoreApplication>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>

namespace StudioUpdate
{
namespace
{
constexpr int kTimeoutMs = 15000;
constexpr int kMaximumRedirects = 5;
constexpr int kMaximumManifestBytes = 1024 * 1024;
}

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent)
{
  _timeout.setSingleShot(true);
  connect(&_timeout, &QTimer::timeout, this, [this]() {
    if (_reply)
    {
      QNetworkReply* reply = _reply;
      _reply = nullptr;
      disconnect(reply, nullptr, this, nullptr);
      reply->abort();
      reply->deleteLater();
    }
    finishWithError(tr("Update check timed out."));
  });
}

QUrl UpdateChecker::manifestUrl(const QString& channel)
{
  const QByteArray overrideValue = qgetenv(channel.compare(QStringLiteral("beta"),
                                                           Qt::CaseInsensitive) == 0
                                                 ? "PJ_STUDIO_UPDATE_MANIFEST_BETA_URL"
                                                 : "PJ_STUDIO_UPDATE_MANIFEST_STABLE_URL");
  if (!overrideValue.isEmpty())
  {
    return QUrl(QString::fromUtf8(overrideValue));
  }
  return QUrl(QStringLiteral(
                  "https://github.com/Kea128/HW_RosPlotJugglerStudio/releases/latest/download/"
                  "update-manifest-%1.json")
                  .arg(channel.toLower()));
}

void UpdateChecker::check(const QString& channel, Mode mode)
{
  cancel();
  _channel = channel.compare(QStringLiteral("beta"), Qt::CaseInsensitive) == 0
                 ? QStringLiteral("beta")
                 : QStringLiteral("stable");
  _mode = mode;
  _redirects = 0;
  emit checkStarted(mode);
  request(manifestUrl(_channel));
}

void UpdateChecker::cancel()
{
  _timeout.stop();
  if (_reply)
  {
    disconnect(_reply, nullptr, this, nullptr);
    _reply->abort();
    _reply->deleteLater();
    _reply = nullptr;
  }
}

bool UpdateChecker::isChecking() const
{
  return !_reply.isNull();
}

void UpdateChecker::request(const QUrl& url)
{
  QString urlError;
  const bool isEnvironmentOverride =
      url == manifestUrl(_channel) &&
      !qgetenv(_channel == QStringLiteral("beta") ? "PJ_STUDIO_UPDATE_MANIFEST_BETA_URL"
                                                   : "PJ_STUDIO_UPDATE_MANIFEST_STABLE_URL")
           .isEmpty();
  if ((!isEnvironmentOverride && !UpdateManifest::isAllowedHttpsUrl(url, &urlError)) ||
      (isEnvironmentOverride && !url.isValid()))
  {
    finishWithError(urlError.isEmpty() ? tr("Invalid test manifest URL.") : urlError);
    return;
  }

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("RosPlotJugglerStudio/%1 updater")
                        .arg(QCoreApplication::applicationVersion()));
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  _reply = _manager.get(request);
  _timeout.start(kTimeoutMs);

  connect(_reply, &QNetworkReply::finished, this, [this]() {
    if (!_reply)
    {
      return;
    }
    _timeout.stop();
    QNetworkReply* reply = _reply;
    _reply = nullptr;

    if (reply->error() != QNetworkReply::NoError)
    {
      const QString message = reply->errorString();
      reply->deleteLater();
      finishWithError(message);
      return;
    }
    const QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (!redirect.isEmpty())
    {
      const QUrl resolved = reply->url().resolved(redirect);
      reply->deleteLater();
      QString error;
      if (++_redirects > kMaximumRedirects ||
          !UpdateManifest::isAllowedHttpsUrl(resolved, &error))
      {
        finishWithError(_redirects > kMaximumRedirects ? tr("Too many update redirects.") : error);
        return;
      }
      this->request(resolved);
      return;
    }

    const QByteArray body = reply->read(kMaximumManifestBytes + 1);
    reply->deleteLater();
    if (body.size() > kMaximumManifestBytes)
    {
      finishWithError(tr("Update manifest is too large."));
      return;
    }
    UpdateManifest manifest;
    QString error;
    if (!UpdateManifest::parse(body, &manifest, &error) ||
        !manifest.matches(currentPlatform(), currentArchitecture(), _channel))
    {
      finishWithError(error.isEmpty() ? tr("Update manifest does not match this system.") : error);
      return;
    }
    if (manifest.isNewerThan(QCoreApplication::applicationVersion()))
    {
      emit updateAvailable(manifest, _mode);
    }
    else
    {
      emit upToDate(_mode);
    }
  });
}

void UpdateChecker::finishWithError(const QString& error)
{
  _timeout.stop();
  emit checkFailed(error, _mode);
}

}  // namespace StudioUpdate

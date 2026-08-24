#include "update_downloader.h"

#include <QCoreApplication>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace StudioUpdate
{
namespace
{
constexpr int kInactivityTimeoutMs = 30000;
constexpr int kMaximumRedirects = 5;
}

UpdateDownloader::UpdateDownloader(QObject* parent)
  : UpdateDownloader(new QNetworkAccessManager, parent)
{
  _manager->setParent(this);
}

UpdateDownloader::UpdateDownloader(QNetworkAccessManager* manager, QObject* parent)
  : QObject(parent), _manager(manager)
{
  Q_ASSERT(_manager);
  _timeout.setSingleShot(true);
  connect(&_timeout, &QTimer::timeout, this,
          [this]() { fail(tr("Update download timed out.")); });
}

void UpdateDownloader::download(const UpdateManifest& manifest, const QString& destination)
{
  if (_reply || _file)
  {
    emit failed(tr("An update download is already in progress."));
    return;
  }
  _cancelling = false;
  _manifest = manifest;
  _destination = destination;
  _received = 0;
  _redirects = 0;
  _hash.reset();
  _file = std::make_unique<QSaveFile>(destination);
  if (!_file->open(QIODevice::WriteOnly))
  {
    fail(tr("Cannot create update file: %1").arg(_file->errorString()));
    return;
  }
  request(manifest.assetUrl);
}

void UpdateDownloader::request(const QUrl& url)
{
  QString error;
  if (!UpdateManifest::isAllowedHttpsUrl(url, &error))
  {
    fail(error);
    return;
  }
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("RosPlotJugglerStudio/%1 updater")
                        .arg(QCoreApplication::applicationVersion()));
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::ManualRedirectPolicy);
  _reply = _manager->get(request);
  _timeout.start(kInactivityTimeoutMs);

  connect(_reply, &QIODevice::readyRead, this, [this]() {
    _timeout.start(kInactivityTimeoutMs);
    if (_reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl().isEmpty())
    {
      consumeAvailableData();
    }
    else
    {
      _reply->readAll();
    }
  });
  connect(_reply, &QNetworkReply::downloadProgress, this,
          [this](qint64 received, qint64) { emit progress(received, _manifest.size); });
  connect(_reply, &QNetworkReply::finished, this, [this]() {
    if (!_reply)
    {
      return;
    }
    _timeout.stop();
    QNetworkReply* reply = _reply;
    const QUrl redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (redirect.isEmpty())
    {
      consumeAvailableData();
    }
    else
    {
      reply->readAll();
    }
    if (!_reply)
    {
      return;
    }
    _reply = nullptr;
    if (_cancelling)
    {
      reply->deleteLater();
      reset();
      emit cancelled();
      return;
    }
    if (reply->error() != QNetworkReply::NoError)
    {
      const QString message = reply->errorString();
      reply->deleteLater();
      fail(message);
      return;
    }
    if (!redirect.isEmpty())
    {
      const QUrl resolved = reply->url().resolved(redirect);
      reply->deleteLater();
      QString error;
      if (++_redirects > kMaximumRedirects ||
          !UpdateManifest::isAllowedHttpsUrl(resolved, &error))
      {
        fail(_redirects > kMaximumRedirects ? tr("Too many download redirects.") : error);
        return;
      }
      this->request(resolved);
      return;
    }
    reply->deleteLater();

    if (_received != _manifest.size)
    {
      fail(tr("Downloaded size mismatch (expected %1, received %2).")
               .arg(_manifest.size)
               .arg(_received));
      return;
    }
    if (_hash.result().toHex().toLower() != _manifest.sha256)
    {
      fail(tr("Downloaded update failed SHA-256 verification."));
      return;
    }
    if (!_file || !_file->commit())
    {
      fail(tr("Cannot finalize update file."));
      return;
    }
    const QString path = _destination;
    reset();
    emit completed(path);
  });
}

void UpdateDownloader::consumeAvailableData()
{
  if (!_reply || !_file)
  {
    return;
  }
  while (_reply->bytesAvailable() > 0)
  {
    const QByteArray chunk = _reply->read(64 * 1024);
    if (chunk.isEmpty())
    {
      break;
    }
    if (_received > _manifest.size - chunk.size() || _file->write(chunk) != chunk.size())
    {
      fail(tr("Update download exceeds the declared size or cannot be written."));
      return;
    }
    _hash.addData(chunk);
    _received += chunk.size();
  }
}

void UpdateDownloader::cancel()
{
  if (!_reply && !_file)
  {
    return;
  }
  _cancelling = true;
  _timeout.stop();
  if (_reply)
  {
    _reply->abort();
  }
  else
  {
    reset();
    emit cancelled();
  }
}

bool UpdateDownloader::isDownloading() const
{
  return !_reply.isNull();
}

void UpdateDownloader::fail(const QString& error)
{
  _timeout.stop();
  if (_reply)
  {
    disconnect(_reply, nullptr, this, nullptr);
    _reply->abort();
    _reply->deleteLater();
    _reply = nullptr;
  }
  reset();
  emit failed(error);
}

void UpdateDownloader::reset()
{
  if (_file)
  {
    if (_file->isOpen())
    {
      _file->cancelWriting();
    }
    _file.reset();
  }
  _reply = nullptr;
  _cancelling = false;
  _received = 0;
}

}  // namespace StudioUpdate

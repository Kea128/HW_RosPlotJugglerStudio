#pragma once

#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>

namespace StudioUpdate
{

// Applies the operating-system proxy configuration to update traffic only. Enabling it
// globally would also route streaming plugin sockets (for example robot WebSockets on
// private networks) through a corporate proxy.
class SystemProxyFactory : public QNetworkProxyFactory
{
public:
  QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override
  {
    return QNetworkProxyFactory::systemProxyForQuery(query);
  }
};

inline void useSystemProxy(QNetworkAccessManager& manager)
{
  manager.setProxyFactory(new SystemProxyFactory());
}

}  // namespace StudioUpdate

#pragma once

#include <memory>
#include <vector>
#include <QPluginLoader>
#include "PlotJuggler/dataloader_base.h"
#include "PlotJuggler/statepublisher_base.h"
#include "PlotJuggler/toolbox_base.h"
#include "PlotJuggler/datastreamer_base.h"
#include "PlotJuggler/messageparser_base.h"

namespace PJ
{
class PluginManager
{
public:
  PluginManager() = default;
  ~PluginManager() = default;
  PluginManager(const PluginManager&) = delete;
  PluginManager& operator=(const PluginManager&) = delete;

  void setEnabledPlugins(const QStringList& enabled_plugins);
  void setDisabledPlugins(const QStringList& disabled_plugins);

  void loadPluginsFromFolder(const QString& folderPath);

  const std::map<QString, DataLoaderPtr>& dataLoaders() const;
  const std::map<QString, StatePublisherPtr>& statePublishers() const;
  const std::map<QString, DataStreamerPtr>& dataStreamers() const;
  const std::map<QString, ToolboxPluginPtr>& toolboxes() const;
  const std::map<QString, ParserFactoryPtr>& parserFactories() const;

  void unloadAllPlugins();

private:
  // Keep native libraries loaded for at least as long as their QObject instances.
  // QPluginLoader owns the root plugin object; interface shared_ptrs are non-owning.
  std::vector<std::unique_ptr<QPluginLoader>> _plugin_loaders;
  QStringList _enabled_plugins;
  QStringList _disabled_plugins;
  bool _test_plugins_enabled = false;

  std::set<QString> _scanned_plugin_files;
  std::set<QString> _loaded_plugins;
  std::map<QString, DataLoaderPtr> _data_loader;
  std::map<QString, StatePublisherPtr> _state_publisher;
  std::map<QString, DataStreamerPtr> _data_streamer;
  std::map<QString, ToolboxPluginPtr> _toolboxes;
  std::map<QString, ParserFactoryPtr> _parser_factories;

  void loadPlugin(const QString& pluginPath);
  void loadWASM(const QString& pluginPath);
};

}  // namespace PJ

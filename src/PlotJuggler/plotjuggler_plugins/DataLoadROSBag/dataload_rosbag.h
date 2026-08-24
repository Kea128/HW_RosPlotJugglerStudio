#pragma once

#include <QObject>
#include <QtPlugin>

#include "PlotJuggler/dataloader_base.h"

class DataLoadROSBag : public PJ::DataLoader
{
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "facontidavide.PlotJuggler3.DataLoader")
  Q_INTERFACES(PJ::DataLoader)

public:
  const char* name() const override
  {
    return "ROS Bag (ROS1 / ROS2)";
  }

  const std::vector<const char*>& compatibleFileExtensions() const override;

  bool readDataFromFile(PJ::FileLoadInfo* fileload_info,
                        PJ::PlotDataMapRef& destination) override;
};

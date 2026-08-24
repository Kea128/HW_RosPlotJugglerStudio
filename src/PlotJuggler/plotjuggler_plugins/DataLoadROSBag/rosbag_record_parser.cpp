#include "rosbag_record_parser.h"

#include <QUrl>

#include <cmath>
#include <limits>

namespace PJ
{
namespace ROSBag
{

namespace
{
bool parseTimestamp(const QByteArray& text, double& seconds)
{
  bool ok = false;
  const qlonglong nanoseconds = text.toLongLong(&ok);
  if (!ok)
  {
    return false;
  }
  seconds = static_cast<double>(nanoseconds) * 1.0e-9;
  return std::isfinite(seconds);
}

bool validName(const QByteArray& encoded, std::string& decoded)
{
  if (encoded.isEmpty() || encoded.size() > 1024 * 1024)
  {
    return false;
  }
  const QString name = QUrl::fromPercentEncoding(encoded);
  if (name.isEmpty() || name.contains(QChar::Null))
  {
    return false;
  }
  decoded = name.toUtf8().toStdString();
  return true;
}
}  // namespace

RecordParser::RecordParser(PlotDataMapRef& destination) : _destination(destination)
{
}

bool RecordParser::parse(const QByteArray& record)
{
  const QList<QByteArray> fields = record.trimmed().split('\t');
  if (fields.size() != 4 || (fields[0] != "N" && fields[0] != "S"))
  {
    return false;
  }

  double timestamp = 0.0;
  std::string name;
  if (!parseTimestamp(fields[1], timestamp) || !validName(fields[2], name))
  {
    return false;
  }

  if (fields[0] == "N")
  {
    bool ok = false;
    const double value = fields[3].toDouble(&ok);
    if (!ok || !std::isfinite(value))
    {
      return false;
    }

    PlotData* series = nullptr;
    const auto cached = _numeric_cache.find(name);
    if (cached == _numeric_cache.end())
    {
      series = &_destination.getOrCreateNumeric(name);
      _numeric_cache.emplace(name, series);
    }
    else
    {
      series = cached->second;
    }
    series->pushBack({ timestamp, value });
  }
  else
  {
    const QByteArray decoded = QByteArray::fromBase64(fields[3], QByteArray::AbortOnBase64DecodingErrors);
    if (decoded.isNull())
    {
      return false;
    }

    StringSeries* series = nullptr;
    const auto cached = _string_cache.find(name);
    if (cached == _string_cache.end())
    {
      series = &_destination.getOrCreateStringSeries(name);
      _string_cache.emplace(name, series);
    }
    else
    {
      series = cached->second;
    }
    series->pushBack({ timestamp, StringRef(decoded.constData(), decoded.size()) });
  }

  ++_record_count;
  return true;
}

size_t RecordParser::recordCount() const
{
  return _record_count;
}

}  // namespace ROSBag
}  // namespace PJ
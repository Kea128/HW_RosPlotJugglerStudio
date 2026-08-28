#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

#include <vector>

class QDialogButtonBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;

namespace PJ
{
namespace ROSBag
{

struct TopicInfo
{
  QString name;
  QString type;
  qint64 messageCount = 0;
};

class TopicDialog : public QDialog
{
public:
  TopicDialog(const std::vector<TopicInfo>& topics, const QString& settingsKey,
              QWidget* parent = nullptr);

  QStringList selectedTopics() const;
  int maxArraySize() const;

protected:
  void accept() override;

private:
  void selectVisible(bool selected);
  void updateFilter(const QString& text);
  void updateAcceptState();

  QString _settings_key;
  QLineEdit* _filter = nullptr;
  QTableWidget* _table = nullptr;
  QSpinBox* _max_array_size = nullptr;
  QDialogButtonBox* _buttons = nullptr;
};

}  // namespace ROSBag
}  // namespace PJ

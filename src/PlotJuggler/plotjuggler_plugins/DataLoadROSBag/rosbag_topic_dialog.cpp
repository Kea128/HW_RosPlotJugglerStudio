#include "rosbag_topic_dialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace PJ
{
namespace ROSBag
{

TopicDialog::TopicDialog(const std::vector<TopicInfo>& topics, const QString& settingsKey,
                         QWidget* parent)
  : QDialog(parent), _settings_key(settingsKey)
{
  setWindowTitle(tr("Select ROS bag topics"));
  resize(760, 520);

  auto* root_layout = new QVBoxLayout(this);
  auto* hint = new QLabel(
      tr("Only selected topics will be read and decoded. Excluding image, point-cloud, and "
         "other high-volume topics can reduce loading time substantially."),
      this);
  hint->setWordWrap(true);
  root_layout->addWidget(hint);

  auto* filter_layout = new QHBoxLayout();
  _filter = new QLineEdit(this);
  _filter->setPlaceholderText(tr("Filter topics..."));
  auto* select_all = new QPushButton(tr("Select all"), this);
  auto* select_none = new QPushButton(tr("Select none"), this);
  filter_layout->addWidget(_filter, 1);
  filter_layout->addWidget(select_all);
  filter_layout->addWidget(select_none);
  root_layout->addLayout(filter_layout);

  _table = new QTableWidget(static_cast<int>(topics.size()), 3, this);
  _table->setHorizontalHeaderLabels(
      { tr("Topic"), tr("Message type"), tr("Messages") });
  _table->setSelectionBehavior(QAbstractItemView::SelectRows);
  _table->setSelectionMode(QAbstractItemView::MultiSelection);
  _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  _table->verticalHeader()->setVisible(false);
  _table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  _table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  _table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

  const QSettings settings;
  restoreGeometry(
      settings.value(QStringLiteral("ROSBagTopicDialog/geometry")).toByteArray());
  const QStringList saved_topics =
      settings.value(_settings_key + QStringLiteral("/topics")).toStringList();
  const bool has_saved_selection =
      settings.contains(_settings_key + QStringLiteral("/topics"));
  for (int row = 0; row < static_cast<int>(topics.size()); ++row)
  {
    const TopicInfo& topic = topics[static_cast<size_t>(row)];
    _table->setItem(row, 0, new QTableWidgetItem(topic.name));
    _table->setItem(row, 1, new QTableWidgetItem(topic.type));
    auto* count_item = new QTableWidgetItem();
    count_item->setData(Qt::DisplayRole, topic.messageCount);
    _table->setItem(row, 2, count_item);
    if (!has_saved_selection || saved_topics.contains(topic.name))
    {
      _table->selectionModel()->select(
          _table->model()->index(row, 0),
          QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
  }
  root_layout->addWidget(_table, 1);

  auto* options_layout = new QHBoxLayout();
  options_layout->addWidget(new QLabel(tr("Maximum array elements per field:"), this));
  _max_array_size = new QSpinBox(this);
  _max_array_size->setRange(0, 100000);
  _max_array_size->setValue(
      settings.value(_settings_key + QStringLiteral("/maxArray"), 100).toInt());
  _max_array_size->setToolTip(
      tr("Large arrays such as images and point clouds create many curves. Set to 0 to skip "
         "array elements."));
  options_layout->addWidget(_max_array_size);
  options_layout->addStretch(1);
  root_layout->addLayout(options_layout);

  _buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  root_layout->addWidget(_buttons);

  connect(_filter, &QLineEdit::textChanged, this, &TopicDialog::updateFilter);
  connect(select_all, &QPushButton::clicked, this, [this]() { selectVisible(true); });
  connect(select_none, &QPushButton::clicked, this, [this]() { selectVisible(false); });
  connect(_table, &QTableWidget::itemSelectionChanged, this, &TopicDialog::updateAcceptState);
  connect(_buttons, &QDialogButtonBox::accepted, this, &TopicDialog::accept);
  connect(_buttons, &QDialogButtonBox::rejected, this, &TopicDialog::reject);
  updateAcceptState();
}

QStringList TopicDialog::selectedTopics() const
{
  QStringList topics;
  const QModelIndexList rows = _table->selectionModel()->selectedRows(0);
  topics.reserve(rows.size());
  for (const QModelIndex& index : rows)
  {
    topics.push_back(index.data(Qt::DisplayRole).toString());
  }
  topics.sort();
  return topics;
}

int TopicDialog::maxArraySize() const
{
  return _max_array_size->value();
}

void TopicDialog::accept()
{
  const QStringList topics = selectedTopics();
  if (topics.isEmpty())
  {
    return;
  }
  QSettings settings;
  settings.setValue(_settings_key + QStringLiteral("/topics"), topics);
  settings.setValue(_settings_key + QStringLiteral("/maxArray"), maxArraySize());
  settings.setValue(QStringLiteral("ROSBagTopicDialog/geometry"), saveGeometry());
  QDialog::accept();
}

void TopicDialog::selectVisible(bool selected)
{
  _table->setUpdatesEnabled(false);
  for (int row = 0; row < _table->rowCount(); ++row)
  {
    if (_table->isRowHidden(row))
    {
      continue;
    }
    if (selected)
    {
      _table->selectionModel()->select(
          _table->model()->index(row, 0),
          QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    else
    {
      _table->selectionModel()->select(
          _table->model()->index(row, 0),
          QItemSelectionModel::Deselect | QItemSelectionModel::Rows);
    }
  }
  _table->setUpdatesEnabled(true);
  updateAcceptState();
}

void TopicDialog::updateFilter(const QString& text)
{
  const QStringList terms = text.split(' ', Qt::SkipEmptyParts);
  for (int row = 0; row < _table->rowCount(); ++row)
  {
    const QString topic = _table->item(row, 0)->text();
    const QString type = _table->item(row, 1)->text();
    bool matches = true;
    for (const QString& term : terms)
    {
      if (!topic.contains(term, Qt::CaseInsensitive) &&
          !type.contains(term, Qt::CaseInsensitive))
      {
        matches = false;
        break;
      }
    }
    _table->setRowHidden(row, !matches);
  }
}

void TopicDialog::updateAcceptState()
{
  if (QPushButton* ok = _buttons->button(QDialogButtonBox::Ok))
  {
    ok->setEnabled(!_table->selectionModel()->selectedRows().isEmpty());
  }
}

}  // namespace ROSBag
}  // namespace PJ

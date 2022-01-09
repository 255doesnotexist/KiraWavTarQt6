#include "extracttargetselectmodel.h"
#include <QJsonArray>
#include <QJsonObject>
#include "wavtar_utils.h"

using namespace wavtar_utils;

namespace  {
    enum COLUMN_INDEX{
        FILENAME = 0, BEGIN_INDEX, LENGTH
    };
}

ExtractTargetSelectModel::ExtractTargetSelectModel(QJsonArray* descArray, QObject *parent)
    : QAbstractTableModel(parent), descArray(descArray)
{
}

int ExtractTargetSelectModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return descArray->count();
}

int ExtractTargetSelectModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    //FileName, begin_index, length
    return 3;
}

QVariant ExtractTargetSelectModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    auto currentObj = descArray->at(index.row()).toObject();
    auto currentSelected = currentObj.value("selected").toBool(true);
    auto currentFileName = currentObj.value("file_name").toString();
    auto currentBeginIndex = QString("%1 样本").arg(decodeBase64<qint64>(currentObj.value("begin_index").toString()));
    auto currentLength = QString("%1 样本").arg(decodeBase64<qint64>(currentObj.value("length").toString()));

    switch (index.column()) {
    case FILENAME:
        if (role == Qt::CheckStateRole)
            return currentSelected ? Qt::Checked : Qt::Unchecked;
        if (role == Qt::DisplayRole)
            return currentFileName;
    case BEGIN_INDEX:
        if (role == Qt::DisplayRole)
            return currentBeginIndex;
    case LENGTH:
        if (role == Qt::DisplayRole)
            return currentLength;
    }

    return QVariant();
}


Qt::ItemFlags ExtractTargetSelectModel::flags(const QModelIndex& index) const
{
    if (index.column() == FILENAME)
        return QAbstractTableModel::flags(index) | Qt::ItemFlag::ItemIsUserCheckable;
    return QAbstractTableModel::flags(index);
}


bool ExtractTargetSelectModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (index.column() == FILENAME && role == Qt::CheckStateRole){
        auto currentObj = descArray->at(index.row()).toObject();
        currentObj.insert("selected", value.value<Qt::CheckState>() == Qt::Checked);
        descArray->replace(index.row(), currentObj);
        emit dataChanged(index, index, {Qt::CheckStateRole});
    }
    return false;
}

void ExtractTargetSelectModel::selectAll()
{
    for (auto i = 0; i < rowCount(); ++i){
        setData(index(i, FILENAME), Qt::Checked, Qt::CheckStateRole);
    }
    emit dataChanged(index(0, FILENAME), index(rowCount(), FILENAME), {Qt::CheckStateRole});
}

void ExtractTargetSelectModel::unselectAll()
{
    for (auto i = 0; i < rowCount(); ++i){
        setData(index(i, FILENAME), Qt::Unchecked, Qt::CheckStateRole);
    }
    emit dataChanged(index(0, FILENAME), index(rowCount(), FILENAME), {Qt::CheckStateRole});
}

void ExtractTargetSelectModel::reverseSelect()
{
    for (auto i = 0; i < rowCount(); ++i){
        auto currentState = data(index(i, FILENAME), Qt::CheckStateRole).value<Qt::CheckState>();
        auto targetState = currentState == Qt::Checked ? Qt::Unchecked : Qt::Checked;
        setData(index(i, FILENAME), targetState, Qt::CheckStateRole);
    }
    emit dataChanged(index(0, FILENAME), index(rowCount(), FILENAME), {Qt::CheckStateRole});
}


QVariant ExtractTargetSelectModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        switch (section){
        case FILENAME: return tr("文件名");
        case BEGIN_INDEX: return tr("开始位置");
        case LENGTH: return tr("长度");
        }
    }
    return {};
}

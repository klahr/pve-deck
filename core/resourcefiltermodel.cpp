// SPDX-License-Identifier: GPL-3.0-or-later
#include "resourcefiltermodel.h"

#include "resourcemodel.h"

ResourceFilterModel::ResourceFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    connect(this, &QAbstractItemModel::rowsInserted, this, &ResourceFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &ResourceFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &ResourceFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &ResourceFilterModel::countChanged);
}

ResourceModel *ResourceFilterModel::resources() const
{
    return m_resources;
}

void ResourceFilterModel::setResources(ResourceModel *resources)
{
    if (resources == m_resources)
        return;
    m_resources = resources;
    setSourceModel(resources);
    emit resourcesChanged();
    emit countChanged();
}

QString ResourceFilterModel::searchText() const
{
    return m_searchText;
}

void ResourceFilterModel::setSearchText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed == m_searchText)
        return;
    m_searchText = trimmed;
    invalidateFilter();
    emit searchTextChanged();
    emit countChanged();
}

ResourceFilterModel::Show ResourceFilterModel::show() const
{
    return m_show;
}

void ResourceFilterModel::setShow(Show show)
{
    if (show == m_show)
        return;
    m_show = show;
    invalidateFilter();
    emit showChanged();
    emit countChanged();
}

int ResourceFilterModel::count() const
{
    return rowCount();
}

QVariantMap ResourceFilterModel::get(int row) const
{
    if (!m_resources || row < 0 || row >= rowCount())
        return QVariantMap();
    return m_resources->get(mapToSource(index(row, 0)).row());
}

bool ResourceFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    const QString status = idx.data(ResourceModel::StatusRole).toString();
    const QString type = idx.data(ResourceModel::TypeRole).toString();
    const bool isTemplate = idx.data(ResourceModel::TemplateRole).toBool();

    switch (m_show) {
    case ShowAll:
        break;
    case ShowRunning:
        if (status != QLatin1String("running"))
            return false;
        break;
    case ShowStopped:
        if (status != QLatin1String("stopped") || isTemplate)
            return false;
        break;
    case ShowVirtualMachines:
        if (type != QLatin1String("qemu"))
            return false;
        break;
    case ShowContainers:
        if (type != QLatin1String("lxc"))
            return false;
        break;
    }

    if (m_searchText.isEmpty())
        return true;
    if (idx.data(ResourceModel::NameRole).toString().contains(m_searchText, Qt::CaseInsensitive)
            || idx.data(ResourceModel::VmidRole).toString().contains(m_searchText)
            || idx.data(ResourceModel::NodeRole).toString().contains(m_searchText, Qt::CaseInsensitive))
        return true;
    foreach (const QString &tag, idx.data(ResourceModel::TagsRole).toStringList()) {
        if (tag.contains(m_searchText, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

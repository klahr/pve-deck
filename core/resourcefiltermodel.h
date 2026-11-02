// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PVEDECK_RESOURCEFILTERMODEL_H
#define PVEDECK_RESOURCEFILTERMODEL_H

#include <QPointer>
#include <QSortFilterProxyModel>

#include "resourcemodel.h"

// Search and filter on top of a ResourceModel. Keeps the source order
// (node, vmid), so section headers per node still work.
class ResourceFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(ResourceModel *resources READ resources WRITE setResources NOTIFY resourcesChanged)
    // Matches name, vmid, node and tags, case-insensitively.
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(Show show READ show WRITE setShow NOTIFY showChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Show {
        ShowAll,
        ShowRunning,
        ShowStopped,
        ShowVirtualMachines,
        ShowContainers
    };
    Q_ENUM(Show)

    explicit ResourceFilterModel(QObject *parent = nullptr);

    ResourceModel *resources() const;
    void setResources(ResourceModel *resources);
    QString searchText() const;
    void setSearchText(const QString &text);
    Show show() const;
    void setShow(Show show);
    int count() const;

    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void resourcesChanged();
    void searchTextChanged();
    void showChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QPointer<ResourceModel> m_resources;
    QString m_searchText;
    Show m_show = ShowAll;
};

#endif // PVEDECK_RESOURCEFILTERMODEL_H

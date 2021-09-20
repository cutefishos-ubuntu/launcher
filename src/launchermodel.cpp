/*
 * Copyright (C) 2021 CutefishOS.
 *
 * Author:     Reion Wong <reion@cutefishos.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "launchermodel.h"
#include "desktopproperties.h"

#include <QDBusInterface>
#include <QDBusPendingCallWatcher>

#include <QtConcurrent/QtConcurrentRun>
#include <QRegularExpression>
#include <QFileSystemWatcher>
#include <QScopedPointer>
#include <QDirIterator>
#include <QProcess>
#include <QDebug>
#include <QIcon>
#include <QDir>

static QByteArray detectDesktopEnvironment()
{
    const QByteArray desktop = qgetenv("XDG_CURRENT_DESKTOP");

    if (!desktop.isEmpty())
        return desktop.toUpper();

    return QByteArray("UNKNOWN");
}

LauncherModel::LauncherModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_settings("cutefishos", "launcher-applist", this)
    , m_mode(NormalMode)
    , m_needSort(false)
{
    // Init datas.
    QByteArray listByteArray = m_settings.value("list").toByteArray();
    QDataStream in(&listByteArray, QIODevice::ReadOnly);
    in >> m_appItems;

    if (m_appItems.isEmpty())
        m_needSort = true;

    QtConcurrent::run(LauncherModel::refresh, this);

    QFileSystemWatcher *watcher = new QFileSystemWatcher(this);
    watcher->addPath("/usr/share/applications");
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
        QtConcurrent::run(LauncherModel::refresh, this);
    });

    m_saveTimer.setInterval(1000);
    connect(&m_saveTimer, &QTimer::timeout, this, &LauncherModel::save);

    connect(this, &QAbstractItemModel::rowsInserted, this, &LauncherModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &LauncherModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &LauncherModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &LauncherModel::countChanged);
    connect(this, &LauncherModel::refreshed, this, &LauncherModel::onRefreshed);
}

LauncherModel::~LauncherModel()
{
}

int LauncherModel::count() const
{
    return rowCount();
}

int LauncherModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);

    if (m_mode == SearchMode)
        return m_searchItems.size();

    return m_appItems.size();
}

QHash<int, QByteArray> LauncherModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles.insert(AppIdRole, "appId");
    roles.insert(ApplicationRole, "application");
    roles.insert(NameRole, "name");
    roles.insert(GenericNameRole, "genericName");
    roles.insert(CommentRole, "comment");
    roles.insert(IconNameRole, "iconName");
    roles.insert(CategoriesRole, "categories");
    roles.insert(FilterInfoRole, "filterInfo");
    roles.insert(PinnedRole, "pinned");
    roles.insert(PinnedIndexRole, "pinnedIndex");
    return roles;
}

QVariant LauncherModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    AppItem appItem = m_mode == NormalMode ? m_appItems.at(index.row())
                                           : m_searchItems.at(index.row());

    switch (role) {
    case AppIdRole:
        return appItem.id;
    case NameRole:
        return appItem.name;
    case IconNameRole:
        return appItem.iconName;
    case FilterInfoRole:
        return QString(appItem.name + QStringLiteral(" ")
                       + appItem.genericName
                       + QStringLiteral(" ")
                       + appItem.comment);
    default:
        return QVariant();
    }
}

void LauncherModel::search(const QString &key)
{
    m_mode = key.isEmpty() ? NormalMode : SearchMode;
    m_searchItems.clear();

    for (const AppItem &item : qAsConst(m_appItems)) {
        const QString &name = item.name;
        const QString &fileName = item.id;

        if (name.contains(key, Qt::CaseInsensitive) ||
                fileName.contains(key, Qt::CaseInsensitive)) {
            m_searchItems.append(item);
            continue;
        }
    }

    emit layoutChanged();
}

void LauncherModel::sendToDock(const QString &key)
{
    int index = findById(key);

    if (index != -1) {
        QDBusMessage message = QDBusMessage::createMethodCall("com.cutefish.Dock",
                                                              "/Dock",
                                                              "com.cutefish.Dock",
                                                               "add");
        message.setArguments(QList<QVariant>() << key);
        QDBusConnection::sessionBus().asyncCall(message);
    }
}

void LauncherModel::removeFromDock(const QString &desktop)
{
    int index = findById(desktop);

    if (index != -1) {
        QDBusMessage message = QDBusMessage::createMethodCall("com.cutefish.Dock",
                                                              "/Dock",
                                                              "com.cutefish.Dock",
                                                               "remove");
        message.setArguments(QList<QVariant>() << desktop);
        QDBusConnection::sessionBus().asyncCall(message);
    }
}

int LauncherModel::findById(const QString &id)
{
    for (int i = 0; i < m_appItems.size(); ++i) {
        if (m_appItems.at(i).id == id)
            return i;
    }

    return -1;
}

void LauncherModel::refresh(LauncherModel *manager)
{
    QStringList addedEntries;
    for (const AppItem &item : qAsConst(manager->m_appItems))
        addedEntries.append(item.id);

    QStringList allEntries;
    QDirIterator it("/usr/share/applications", { "*.desktop" }, QDir::NoFilter, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        const auto fileName = it.next();
        if (!QFile::exists(fileName))
            continue;

        allEntries.append(fileName);
    }

    for (const QString &fileName : allEntries) {
        //if (!addedEntries.contains(fileName))
            QMetaObject::invokeMethod(manager, "addApp", Q_ARG(QString, fileName));
    }

    for (const AppItem &item : qAsConst(manager->m_appItems))
        if (!allEntries.contains(item.id))
            QMetaObject::invokeMethod(manager, "removeApp", Q_ARG(QString, item.id));

    // Signal the model was refreshed
    QMetaObject::invokeMethod(manager, "refreshed");
}

void LauncherModel::move(int from, int to, int page, int pageCount)
{
    if (from == to)
        return;

    int newFrom = from + (page * pageCount);
    int newTo = to + (page * pageCount);

    m_appItems.move(newFrom, newTo);

//    if (from < to)
//        beginMoveRows(QModelIndex(), from, from, QModelIndex(), to + 1);
//    else
//        beginMoveRows(QModelIndex(), from, from, QModelIndex(), to);

    //    endMoveRows();

    delaySave();
}

void LauncherModel::save()
{
    m_settings.clear();
    QByteArray datas;
    QDataStream out(&datas, QIODevice::WriteOnly);
    out << m_appItems;
    m_settings.setValue("list", datas);
}

void LauncherModel::delaySave()
{
    if (m_saveTimer.isActive())
        m_saveTimer.stop();

    m_saveTimer.start();
}

bool LauncherModel::launch(const QString &path)
{
    int index = findById(path);

    if (index != -1) {
        const AppItem &item = m_appItems.at(index);
        QStringList args = item.args;
        QScopedPointer<QProcess> p(new QProcess);
        p->setStandardInputFile(QProcess::nullDevice());
        p->setProcessChannelMode(QProcess::ForwardedChannels);

        QString cmd = args.takeFirst();
        p->setProgram(cmd);
        p->setArguments(args);

        // Because launcher has hidden animation,
        // cutefish-screenshot needs to be processed.
        if (cmd == "cutefish-screenshot") {
            p->setArguments(QStringList() << "-d" << "200");
        }

        Q_EMIT applicationLaunched();

        return p->startDetached();
    }

    return false;
}

void LauncherModel::onRefreshed()
{
    if (!m_needSort)
        return;

    m_needSort = false;

    beginResetModel();
    std::sort(m_appItems.begin(), m_appItems.end(), [=] (AppItem &a, AppItem &b) {
        return a.name < b.name;
    });
    endResetModel();

    delaySave();
}

void LauncherModel::addApp(const QString &fileName)
{
    int index = findById(fileName) ;

    DesktopProperties desktop(fileName, "Desktop Entry");

    if (desktop.contains("Terminal") && desktop.value("Terminal").toBool())
        return;

    if (desktop.contains("OnlyShowIn")) {
        const QStringList items = desktop.value("OnlyShowIn").toString().split(';');

        if (!items.contains(detectDesktopEnvironment()))
            return;
    }

    if (desktop.value("NoDisplay").toBool() ||
        desktop.value("Hidden").toBool())
        return;

    QString appName = desktop.value(QString("Name[%1]").arg(QLocale::system().name())).toString();
    QString appExec = desktop.value("Exec").toString();

    if (appName.isEmpty())
        appName = desktop.value("Name").toString();

    appExec.remove(QRegularExpression("%."));
    appExec.remove(QRegularExpression("^\""));
    // appExec.remove(QRegularExpression(" *$"));
    appExec = appExec.replace("\"", "");
    appExec = appExec.simplified();

    // 存在需要更新信息
    if (index >= 0 && index <= m_appItems.size()) {
        AppItem &item = m_appItems[index];
        item.name = appName;
        item.genericName = desktop.value("Comment").toString();
        item.comment = desktop.value("Comment").toString();
        item.iconName = desktop.value("Icon").toString();
        item.args = appExec.split(" ");
        emit dataChanged(LauncherModel::index(index), LauncherModel::index(index));
    } else {
        AppItem appItem;
        appItem.id = fileName;
        appItem.name = appName;
        appItem.genericName = desktop.value("Comment").toString();
        appItem.comment = desktop.value("Comment").toString();
        appItem.iconName = desktop.value("Icon").toString();
        appItem.args = appExec.split(" ");

        beginInsertRows(QModelIndex(), m_appItems.count(), m_appItems.count());
        m_appItems.append(appItem);
        qDebug() << "added: " << appItem.name;
        endInsertRows();

        if (!m_needSort)
            delaySave();
    }
}

void LauncherModel::removeApp(const QString &fileName)
{
    int index = findById(fileName);
    if (index < 0)
        return;

    beginRemoveRows(QModelIndex(), index, index);
    m_appItems.removeAt(index);
    endRemoveRows();

    delaySave();
}

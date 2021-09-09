/*
 * Copyright (C) 2021 CutefishOS.
 *
 * Author:     revenmartin <revenmartin@gmail.com>
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

#ifndef LAUNCHERITEM_H
#define LAUNCHERITEM_H

#include <QObject>

class LauncherItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString fuke READ fuke CONSTANT)

public:
    explicit LauncherItem(QObject *parent = nullptr);

    Q_INVOKABLE QString fuke() { return "asd"; };

    QString id;
    QString name;
    QString genericName;
    QString comment;
    QString iconName;
    QStringList args;
};

#endif // LAUNCHERITEM_H

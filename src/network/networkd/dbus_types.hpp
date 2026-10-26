#pragma once

#include <qdbusargument.h>
#include <qdbusextratypes.h>
#include <qlist.h>
#include <qmetatype.h>
#include <qstring.h>
#include <qtypes.h>

namespace qs::network {

struct NetworkdLinkInfo {
	qint32 ifindex = 0;
	QString name;
	QDBusObjectPath path;
};

using NetworkdLinkList = QList<NetworkdLinkInfo>;

const QDBusArgument& operator>>(const QDBusArgument& argument, NetworkdLinkInfo& info);
const QDBusArgument& operator<<(QDBusArgument& argument, const NetworkdLinkInfo& info);

} // namespace qs::network

Q_DECLARE_METATYPE(qs::network::NetworkdLinkInfo);
Q_DECLARE_METATYPE(qs::network::NetworkdLinkList);

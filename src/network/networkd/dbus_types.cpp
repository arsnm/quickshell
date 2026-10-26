#include "dbus_types.hpp"

namespace qs::network {

const QDBusArgument& operator>>(const QDBusArgument& argument, NetworkdLinkInfo& info) {
	argument.beginStructure();
	argument >> info.ifindex >> info.name >> info.path;
	argument.endStructure();
	return argument;
}

const QDBusArgument& operator<<(QDBusArgument& argument, const NetworkdLinkInfo& info) {
	argument.beginStructure();
	argument << info.ifindex << info.name << info.path;
	argument.endStructure();
	return argument;
}

} // namespace qs::network

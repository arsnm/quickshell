set_source_files_properties(org.freedesktop.network1.Manager.xml PROPERTIES
	CLASSNAME DBusNetworkdManagerProxy
	NO_NAMESPACE TRUE
	INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/dbus_types.hpp
)

qt_add_dbus_interface(NETWORKD_DBUS_INTERFACES
	org.freedesktop.network1.Manager.xml
	dbus_networkd_manager
)

set_source_files_properties(org.freedesktop.network1.Link.xml PROPERTIES
	CLASSNAME DBusNetworkdLinkProxy
	NO_NAMESPACE TRUE
)

qt_add_dbus_interface(NETWORKD_DBUS_INTERFACES
	org.freedesktop.network1.Link.xml
	dbus_networkd_link
)

qt_add_library(quickshell-network-networkd STATIC
	backend.cpp
	wired.cpp
	dbus_types.cpp
	${NETWORKD_DBUS_INTERFACES}
)

target_include_directories(quickshell-network-networkd PUBLIC
	${CMAKE_CURRENT_BINARY_DIR}
	# Allow networkd to find NetworkManager's generated headers without creating a link cycle:
	${CMAKE_CURRENT_BINARY_DIR}/../nm
)

target_link_libraries(quickshell-network-networkd PRIVATE Qt::Qml Qt::DBus)
qs_add_link_dependencies(quickshell-network-networkd quickshell-dbus)

#pragma once

#include <qdbusservicewatcher.h>
#include <qhash.h>
#include <qobject.h>
#include <qproperty.h>
#include <qsocketnotifier.h>
#include <qtmetamacros.h>

#include "../../dbus/properties.hpp"
#include "../enums.hpp"
#include "../qml.hpp"
#include "dbus_networkd_manager.h"
#include "wired.hpp"

namespace qs::network {

class NetworkdBackend: public NetworkBackend {
	Q_OBJECT;

public:
	explicit NetworkdBackend(QObject* parent = nullptr);
	~NetworkdBackend() override;

	[[nodiscard]] bool isAvailable() const override;
	[[nodiscard]] NetworkConnectivity::Enum connectivity() const { return this->bConnectivity; }

signals:
	void deviceAdded(qs::network::NetworkDevice* device);
	void deviceRemoved(qs::network::NetworkDevice* device);
	void connectivityChanged(NetworkConnectivity::Enum state);

private slots:
	void onServiceRegistered();
	void onServiceUnregistered();
	void onNetLinkReadyRead();

private:
	void setupNetlinkWatcher();
	void syncLinks();
	void registerLink(const QString& name, const QString& path);

	QHash<QString, NetworkdWiredDevice*> mDevices;
	int mNetlinkFd = -1;
	QSocketNotifier* mNetlinkNotifier = nullptr;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(NetworkdBackend, QString, bOnlineState);
	Q_OBJECT_BINDABLE_PROPERTY(NetworkdBackend, QString, bOperationalState);
	Q_OBJECT_BINDABLE_PROPERTY(NetworkdBackend, NetworkConnectivity::Enum, bConnectivity, &NetworkdBackend::connectivityChanged);

	QS_DBUS_BINDABLE_PROPERTY_GROUP(NetworkdManagerAdapter, dbusProperties);
    QS_DBUS_PROPERTY_BINDING(NetworkdBackend, pOnlineState, bOnlineState, dbusProperties, "OnlineState");
    QS_DBUS_PROPERTY_BINDING(NetworkdBackend, pOperationalState, bOperationalState, dbusProperties, "OperationalState");
	// clang-format on

	DBusNetworkdManagerProxy* proxy = nullptr;
	QDBusServiceWatcher serviceWatcher;
};

} // namespace qs::network

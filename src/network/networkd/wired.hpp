#pragma once

#include <qobject.h>
#include <qproperty.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../dbus/properties.hpp"
#include "../network.hpp"
#include "../wired.hpp"
#include "dbus_networkd_link.h"

namespace qs::network {

class NetworkdWiredDevice: public QObject {
	Q_OBJECT;

public:
	explicit NetworkdWiredDevice(const QString& name, const QString& path, QObject* parent = nullptr);

	[[nodiscard]] bool isValid() const;
	[[nodiscard]] QString name() const { return this->mName; }
	[[nodiscard]] QString path() const { return this->mPath; }
	[[nodiscard]] WiredDevice* frontend() const { return this->mFrontend; }

signals:
	void loaded();

private slots:
	void initWired();
	void updateSysfsProperties();
	void reconfigure();
	void drop();

private:
	void bindFrontend();

	QString mName;
	QString mPath;
	WiredDevice* mFrontend = nullptr;
	Network* mNetwork = nullptr;

	// clang-format off
    Q_OBJECT_BINDABLE_PROPERTY(NetworkdWiredDevice, QString, bOperationalState);
    Q_OBJECT_BINDABLE_PROPERTY(NetworkdWiredDevice, QString, bCarrierState);
    Q_OBJECT_BINDABLE_PROPERTY(NetworkdWiredDevice, QString, bAddressState);
    Q_OBJECT_BINDABLE_PROPERTY(NetworkdWiredDevice, QString, bAdminState);
    Q_OBJECT_BINDABLE_PROPERTY(NetworkdWiredDevice, QString, bHwAddress);
    Q_OBJECT_BINDABLE_PROPERTY(NetworkdWiredDevice, quint32, bSpeed);

    QS_DBUS_BINDABLE_PROPERTY_GROUP(NetworkdWiredDeviceAdapter, linkProperties);
    QS_DBUS_PROPERTY_BINDING(NetworkdWiredDevice, pOperationalState, bOperationalState, linkProperties, "OperationalState");
    QS_DBUS_PROPERTY_BINDING(NetworkdWiredDevice, pCarrierState, bCarrierState, linkProperties, "CarrierState");
    QS_DBUS_PROPERTY_BINDING(NetworkdWiredDevice, pAddressState, bAddressState, linkProperties, "AddressState");
    QS_DBUS_PROPERTY_BINDING(NetworkdWiredDevice, pAdminState, bAdminState, linkProperties, "AdministrativeState");
	// clang-format on

	DBusNetworkdLinkProxy* linkProxy = nullptr;
};

} // namespace qs::network

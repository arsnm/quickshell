#include "wired.hpp"

#include <qbuffer.h>
#include <qdbusconnection.h>
#include <qhashfunctions.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qstorageinfo.h>

#include "../../core/logcat.hpp"
#include "../enums.hpp"
#include "../network.hpp"
#include "../wired.hpp"

namespace qs::network {
using namespace qs::dbus;

namespace {
QS_LOGGING_CATEGORY(logNetworkd, "quickshell.network.netword", QtWarningMsg);

QString readSysfsLine(const QString& iface, const QString& attr) {
	QFile file(QStringLiteral("/sys/class/net/%1/%2").arg(iface, attr));
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
	return QString::fromUtf8(file.readLine()).trimmed();
}
} // namespace

NetworkdWiredDevice::NetworkdWiredDevice(const QString& name, const QString& path, QObject* parent)
    : QObject(parent)
    , mName(name)
    , mPath(path) {
	this->linkProxy = new DBusNetworkdLinkProxy(
	    "org.freedesktop.network1",
	    path,
	    QDBusConnection::systemBus(),
	    this
	);

	if (!this->linkProxy->isValid()) {
		qCWarning(logNetworkd) << "Cannot create DBus interface for wired link at" << path;
		return;
	}

	QObject::connect(
	    &this->linkProperties,
	    &DBusPropertyGroup::getAllFinished,
	    this,
	    &NetworkdWiredDevice::initWired,
	    Qt::SingleShotConnection
	);

	this->linkProperties.setInterface(this->linkProxy);
	this->linkProperties.updateAllViaGetAll();

	this->mFrontend = new WiredDevice(this);
	this->mNetwork = new Network(name, this->mFrontend, this);
	this->bindFrontend();
}

void NetworkdWiredDevice::initWired() {
	this->updateSysfsProperties();
	emit this->loaded();
}

void NetworkdWiredDevice::updateSysfsProperties() {
	this->bHwAddress = readSysfsLine(this->mName, QStringLiteral("address"));

	bool ok = false;
	const int speed = readSysfsLine(this->mName, QStringLiteral("speed")).toInt(&ok);
	this->bSpeed = (ok && speed > 0) ? static_cast<quint32>(speed) : 0;
}

void NetworkdWiredDevice::bindFrontend() {
	auto* dev = this->mFrontend;
	auto* net = this->mNetwork;

	dev->bindableName().setBinding([this]() { return this->mName; });
	dev->bindableAddress().setBinding([this]() { return this->bHwAddress.value(); });
	dev->bindableLinkSpeed().setBinding([this]() { return this->bSpeed.value(); });
	dev->bindableNmManaged().setBinding([this]() {
		return this->bAdminState.value() != QStringLiteral("unmanaged");
	});

	auto hasCarrier = [this]() {
		const auto& carrier = this->bCarrierState.value();
		if (!carrier.isEmpty()) {
			return carrier == QStringLiteral("carrier") || carrier == QStringLiteral("degraded")
			    || carrier == QStringLiteral("enslaved");
		}
		const auto& op = this->bOperationalState.value();
		return op == QStringLiteral("carrier") || op == QStringLiteral("degraded")
		    || op == QStringLiteral("enslaved") || op == QStringLiteral("routable");
	};

	dev->bindableHasLink().setBinding(hasCarrier);

	auto translateState = [this]() {
		const auto& admin = this->bAdminState.value();
		if (admin == QStringLiteral("unmanaged") || admin == QStringLiteral("linger")) {
			return ConnectionState::Unknown;
		}

		const auto& op = this->bOperationalState.value();
		if (op == QStringLiteral("routable") || op == QStringLiteral("degraded")) {
			return ConnectionState::Connected;
		}

		if (op == QStringLiteral("carrier") || op == QStringLiteral("enslaved")
		    || admin == QStringLiteral("configuring"))
		{
			return ConnectionState::Connecting;
		}

		if (op == QStringLiteral("no-carrier") || op == QStringLiteral("off")
		    || admin == QStringLiteral("dormant"))
		{
			return ConnectionState::Disconnected;
		}
		return ConnectionState::Unknown;
	};

	dev->bindableState().setBinding(translateState);
	net->bindableState().setBinding(translateState);
	net->bindableConnected().setBinding([dev]() {
		return dev->bindableState().value() == ConnectionState::Connected;
	});
	net->bindableKnown().setBinding([dev]() { return dev->bindableNmManaged().value(); });

	// Re-red link speed from sysfs whenever carrier state changes, and toggle network object visibility.
	QObject::connect(dev, &WiredDevice::hasLinkChanged, this, [this, dev, net]() {
		this->updateSysfsProperties();
		if (dev->bindableHasLink().value()) {
			dev->networkAdded(net);
		} else {
			dev->networkRemoved(net);
		}
	});

	QObject::connect(dev, &NetworkDevice::requestDisconnect, this, &NetworkdWiredDevice::drop);
	QObject::connect(net, &Network::requestDisconnect, this, &NetworkdWiredDevice::drop);
	QObject::connect(net, &Network::requestConnect, this, &NetworkdWiredDevice::reconfigure);
}

void NetworkdWiredDevice::reconfigure() { this->linkProxy->Reconfigure(); }
void NetworkdWiredDevice::drop() { this->linkProxy->Drop(); }

bool NetworkdWiredDevice::isValid() const { return this->linkProxy && this->linkProxy->isValid(); }

} // namespace qs::network

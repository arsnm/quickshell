#include "backend.hpp"
#include <array>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <qdbuspendingcall.h>
#include <qdbuspendingreply.h>
#include <qloggingcategory.h>
#include <sys/socket.h>
#include <unistd.h>
//
#include <qbuffer.h>
#include <qdbusconnection.h>
#include <qdbusmetatype.h>
#include <qdbusservicewatcher.h>
#include <qdir.h>
#include <qfileinfo.h>
#include <qlogging.h>
#include <qobject.h>
#include <qsocketnotifier.h>

#include "../../core/logcat.hpp"
#include "dbus_types.hpp"
#include "wired.hpp"

namespace qs::network {

namespace {
QS_LOGGING_CATEGORY(logNetworkd, "quickshell.network.networkd", QtWarningMsg);

// Returns true ONLY for physical/wired Ethernet interfaces (`ARPHRD_ETHER == 1`),
// excluding loopback, Wi-Fi, brigdges, bonds, tun, and virtual devices.
bool isPhysicalEthernet(const QString& iface) {
	const QString basePath = QStringLiteral("/sys/class/net/") + iface;

	QFile typeFile(basePath + QStringLiteral("/type"));
	if (!typeFile.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
	if (typeFile.readLine().trimmed().toInt() != 1) return false; // 1 == ARPHRD_ETHER

	// Exclude Wi-Fi interfaces to not interfere with any other Wi-Fi backend.
	if (QFileInfo::exists(basePath + QStringLiteral("/wireless"))
	    || QFileInfo::exists(basePath + QStringLiteral("/phy80211")))
	{
		return false;
	}

	// Exclude virtual interfaces (veth, bridge, docker, virbr, etc.).
	const QString canonical = QFileInfo(basePath).canonicalFilePath();
	if (canonical.startsWith(QStringLiteral("/sys/devices/virtual/"))) {
		return false;
	}

	return true;
}

} // namespace

NetworkdBackend::NetworkdBackend(QObject* parent): NetworkBackend(parent) {
	qDBusRegisterMetaType<NetworkdLinkInfo>();
	qDBusRegisterMetaType<NetworkdLinkList>();

	auto bus = QDBusConnection::systemBus();
	if (!bus.isConnected()) {
		qCWarning(logNetworkd) << "Could not connet to DBus. systemd-netword backend will not work.";
		return;
	}

	this->bConnectivity.setBinding([this]() {
		const auto& online = this->bOnlineState.value();

		if (online == QStringLiteral("online")) return NetworkConnectivity::Full;
		if (online == QStringLiteral("partial")) return NetworkConnectivity::Limited;
		if (online == QStringLiteral("offline")) return NetworkConnectivity::None;

		const auto& op = this->bOperationalState.value();
		if (op == QStringLiteral("routable")) return NetworkConnectivity::Full;
		if (op == QStringLiteral("degraded") || op == QStringLiteral("carrier")) {
			return NetworkConnectivity::Limited;
		}
		if (op == QStringLiteral("no-carrier") || op == QStringLiteral("off")) {
			return NetworkConnectivity::None;
		}

		return NetworkConnectivity::Unknown;
	});

	QObject::connect(
	    &this->serviceWatcher,
	    &QDBusServiceWatcher::serviceRegistered,
	    this,
	    &NetworkdBackend::onServiceRegistered
	);
	QObject::connect(
	    &this->serviceWatcher,
	    &QDBusServiceWatcher::serviceUnregistered,
	    this,
	    &NetworkdBackend::onServiceUnregistered
	);

	this->serviceWatcher.setWatchMode(
	    QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration
	);
	this->serviceWatcher.addWatchedService("org.freedesktop.network1");
	this->serviceWatcher.setConnection(bus);

	this->proxy = new DBusNetworkdManagerProxy(
	    "org.freedesktop.network1",
	    "/org/freedesktop/network1",
	    bus,
	    this
	);

	this->dbusProperties.setInterface(this->proxy);

	if (!this->proxy->isValid()) {
		qCDebug(
		    logNetworkd
		) << "systemd-networkd is not currently running. This network backend will not work.";
	} else {
		this->setupNetlinkWatcher();
		this->onServiceRegistered();
	}
}

NetworkdBackend::~NetworkdBackend() {
	if (this->mNetlinkFd >= 0) close(this->mNetlinkFd);
}

void NetworkdBackend::setupNetlinkWatcher() {
	// systemd-networkd Manager does not emit LinkAdded/LinkRemoved D-Bus signals.
	// A non-blocking RTNLGRP_LINK socket integrated into Qt's event loop via QSocketNotifier triggers syncLinks() whenever a USB Ethernet dongle is hotplugged or removed.
	this->mNetlinkFd = socket(AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_ROUTE);
	if (this->mNetlinkFd < 0) return;

	sockaddr_nl addr {};
	addr.nl_family = AF_NETLINK;
	addr.nl_groups = RTMGRP_LINK;

	if (bind(this->mNetlinkFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
		close(this->mNetlinkFd);
		this->mNetlinkFd = -1;
		return;
	}

	this->mNetlinkNotifier = new QSocketNotifier(this->mNetlinkFd, QSocketNotifier::Read, this);
	QObject::connect(
	    this->mNetlinkNotifier,
	    &QSocketNotifier::activated,
	    this,
	    &NetworkdBackend::onNetLinkReadyRead
	);
}

void NetworkdBackend::onNetLinkReadyRead() {
	std::array<char, 4096> buf = {};
	while (recv(this->mNetlinkFd, buf.data(), buf.size(), MSG_DONTWAIT) > 0) {}
	if (this->isAvailable()) this->syncLinks();
}

void NetworkdBackend::syncLinks() {
	auto pending = this->proxy->ListLinks();
	auto* call = new QDBusPendingCallWatcher(pending, this);

	QObject::connect(
	    call,
	    &QDBusPendingCallWatcher::finished,
	    this,
	    [this](QDBusPendingCallWatcher* call) {
		    const QDBusPendingReply<NetworkdLinkList> reply = *call;
		    if (reply.isValid()) {
			    QSet<QString> currentPaths;
			    for (const auto& link: reply.value()) {
				    const QString path = link.path.path();
				    if (isPhysicalEthernet(link.name)) {
					    currentPaths.insert(path);
					    if (!this->mDevices.contains(path)) {
						    this->registerLink(link.name, path);
					    }
				    }
			    }

			    // Clean up unplugged USB Ethernet links.
			    const auto existingPaths = this->mDevices.keys();
			    for (const QString& path: existingPaths) {
				    if (!currentPaths.contains(path)) {
					    if (auto* dev = this->mDevices.take(path)) {
						    emit this->deviceRemoved(dev->frontend());
						    delete dev;
					    }
				    }
			    }
		    }
		    delete call;
	    }
	);
}

void NetworkdBackend::registerLink(const QString& name, const QString& path) {
	auto* dev = new NetworkdWiredDevice(name, path, this);
	if (!dev->isValid()) {
		delete dev;
		return;
	}
	this->mDevices.insert(path, dev);
	QObject::connect(dev, &NetworkdWiredDevice::loaded, this, [this, dev]() {
		emit this->deviceAdded(dev->frontend());
	});
}

void NetworkdBackend::onServiceRegistered() {
	this->dbusProperties.updateAllViaGetAll();
	this->syncLinks();
}

void NetworkdBackend::onServiceUnregistered() {
	const auto devices = this->mDevices;
	this->mDevices.clear();
	for (auto* dev: devices) {
		if (!dev) continue;
		emit this->deviceRemoved(dev->frontend());
		delete dev;
	}
}

bool NetworkdBackend::isAvailable() const { return this->proxy && this->proxy->isValid(); }

} // namespace qs::network

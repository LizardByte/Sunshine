#include "nvcomputer.h"
#include "nvapp.h"
#include "settings/compatfetcher.h"

#include <QUdpSocket>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QNetworkProxy>

#define SER_NAME "hostname"
#define SER_UUID "uuid"
#define SER_MAC "mac"
#define SER_LOCALADDR "localaddress"
#define SER_LOCALPORT "localport"
#define SER_REMOTEADDR "remoteaddress"
#define SER_REMOTEPORT "remoteport"
#define SER_MANUALADDR "manualaddress"
#define SER_MANUALPORT "manualport"
#define SER_IPV6ADDR "ipv6address"
#define SER_IPV6PORT "ipv6port"
#define SER_APPLIST "apps"
#define SER_SRVCERT "srvcert"
#define SER_CUSTOMNAME "customname"
#define SER_NVIDIASOFTWARE "nvidiasw"

NvComputer::NvComputer(QSettings& settings)
{
    this->name = settings.value(SER_NAME).toString();
    this->uuid = settings.value(SER_UUID).toString();
    this->hasCustomName = settings.value(SER_CUSTOMNAME).toBool();
    this->macAddress = settings.value(SER_MAC).toByteArray();
    this->localAddress = NvAddress(settings.value(SER_LOCALADDR).toString(),
                                   settings.value(SER_LOCALPORT, QVariant(DEFAULT_HTTP_PORT)).toUInt());
    this->remoteAddress = NvAddress(settings.value(SER_REMOTEADDR).toString(),
                                    settings.value(SER_REMOTEPORT, QVariant(DEFAULT_HTTP_PORT)).toUInt());
    this->ipv6Address = NvAddress(settings.value(SER_IPV6ADDR).toString(),
                                  settings.value(SER_IPV6PORT, QVariant(DEFAULT_HTTP_PORT)).toUInt());
    this->manualAddress = NvAddress(settings.value(SER_MANUALADDR).toString(),
                                    settings.value(SER_MANUALPORT, QVariant(DEFAULT_HTTP_PORT)).toUInt());
    this->serverCert = QSslCertificate(settings.value(SER_SRVCERT).toByteArray());
    this->isNvidiaServerSoftware = settings.value(SER_NVIDIASOFTWARE).toBool();

    int appCount = settings.beginReadArray(SER_APPLIST);
    this->appList.reserve(appCount);
    for (int i = 0; i < appCount; i++) {
        settings.setArrayIndex(i);

        NvApp app(settings);
        this->appList.append(app);
    }
    settings.endArray();
    sortAppList();

    this->currentGameId = 0;
    this->pairState = PS_UNKNOWN;
    this->state = CS_UNKNOWN;
    this->gfeVersion = nullptr;
    this->appVersion = nullptr;
    this->maxLumaPixelsHEVC = 0;
    this->serverCodecModeSupport = 0;
    this->pendingQuit = false;
    this->gpuModel = nullptr;
    this->isSupportedServerVersion = true;
    this->externalPort = this->remoteAddress.port();
    this->activeHttpsPort = 0;
}

void NvComputer::setRemoteAddress(QHostAddress address)
{
    QWriteLocker lock(&this->lock);

    Q_ASSERT(this->externalPort != 0);

    this->remoteAddress = NvAddress(address, this->externalPort);
}

void NvComputer::serialize(QSettings& settings, bool serializeApps) const
{
    QReadLocker lock(&this->lock);

    settings.setValue(SER_NAME, name);
    settings.setValue(SER_CUSTOMNAME, hasCustomName);
    settings.setValue(SER_UUID, uuid);
    settings.setValue(SER_MAC, macAddress);
    settings.setValue(SER_LOCALADDR, localAddress.address());
    settings.setValue(SER_LOCALPORT, localAddress.port());
    settings.setValue(SER_REMOTEADDR, remoteAddress.address());
    settings.setValue(SER_REMOTEPORT, remoteAddress.port());
    settings.setValue(SER_IPV6ADDR, ipv6Address.address());
    settings.setValue(SER_IPV6PORT, ipv6Address.port());
    settings.setValue(SER_MANUALADDR, manualAddress.address());
    settings.setValue(SER_MANUALPORT, manualAddress.port());
    settings.setValue(SER_SRVCERT, serverCert.toPem());
    settings.setValue(SER_NVIDIASOFTWARE, isNvidiaServerSoftware);

    // Avoid deleting an existing applist if we couldn't get one
    if (!appList.isEmpty() && serializeApps) {
        settings.remove(SER_APPLIST);
        settings.beginWriteArray(SER_APPLIST);
        for (int i = 0; i < appList.count(); i++) {
            settings.setArrayIndex(i);
            appList[i].serialize(settings);
        }
        settings.endArray();
    }
}

bool NvComputer::isEqualSerialized(const NvComputer &that) const
{
    return this->name == that.name &&
           this->hasCustomName == that.hasCustomName &&
           this->uuid == that.uuid &&
           this->macAddress == that.macAddress &&
           this->localAddress == that.localAddress &&
           this->remoteAddress == that.remoteAddress &&
           this->ipv6Address == that.ipv6Address &&
           this->manualAddress == that.manualAddress &&
           this->serverCert == that.serverCert &&
           this->isNvidiaServerSoftware == that.isNvidiaServerSoftware &&
           this->appList == that.appList;
}

void NvComputer::sortAppList()
{
    std::stable_sort(appList.begin(), appList.end(), [](const NvApp& app1, const NvApp& app2) {
       return app1.name.toLower() < app2.name.toLower();
    });
}

NvComputer::NvComputer(NvHTTP& http, QString serverInfo)
{
    this->serverCert = http.serverCert();

    this->hasCustomName = false;
    this->name = NvHTTP::getXmlString(serverInfo, "hostname");
    if (this->name.isEmpty()) {
        this->name = "UNKNOWN";
    }

    this->uuid = NvHTTP::getXmlString(serverInfo, "uniqueid");
    QString newMacString = NvHTTP::getXmlString(serverInfo, "mac");
    if (newMacString != "00:00:00:00:00:00") {
        QStringList macOctets = newMacString.split(':');
        for (const QString& macOctet : macOctets) {
            this->macAddress.append((char) macOctet.toInt(nullptr, 16));
        }
    }

    QString codecSupport = NvHTTP::getXmlString(serverInfo, "ServerCodecModeSupport");
    if (!codecSupport.isEmpty()) {
        this->serverCodecModeSupport = codecSupport.toInt();
    }
    else {
        // Assume H.264 is always supported
        this->serverCodecModeSupport = SCM_H264;
    }

    QString maxLumaPixelsHEVC = NvHTTP::getXmlString(serverInfo, "MaxLumaPixelsHEVC");
    if (!maxLumaPixelsHEVC.isEmpty()) {
        this->maxLumaPixelsHEVC = maxLumaPixelsHEVC.toInt();
    }
    else {
        this->maxLumaPixelsHEVC = 0;
    }

    this->displayModes = NvHTTP::getDisplayModeList(serverInfo);
    std::stable_sort(this->displayModes.begin(), this->displayModes.end(),
                     [](const NvDisplayMode& mode1, const NvDisplayMode& mode2) {
        return (uint64_t)mode1.width * mode1.height * mode1.refreshRate <
                (uint64_t)mode2.width * mode2.height * mode2.refreshRate;
    });

    // We can get an IPv4 loopback address if we're using the GS IPv6 Forwarder
    this->localAddress = NvAddress(NvHTTP::getXmlString(serverInfo, "LocalIP"), http.httpPort());
    if (this->localAddress.address().startsWith("127.")) {
        this->localAddress = NvAddress();
    }

    QString httpsPort = NvHTTP::getXmlString(serverInfo, "HttpsPort");
    if (httpsPort.isEmpty() || (this->activeHttpsPort = httpsPort.toUShort()) == 0) {
        this->activeHttpsPort = DEFAULT_HTTPS_PORT;
    }

    // This is an extension which is not present in GFE. It is present for Sunshine to be able
    // to support dynamic HTTP WAN ports without requiring the user to manually enter the port.
    QString remotePortStr = NvHTTP::getXmlString(serverInfo, "ExternalPort");
    if (remotePortStr.isEmpty() || (this->externalPort = remotePortStr.toUShort()) == 0) {
        this->externalPort = http.httpPort();
    }

    QString remoteAddress = NvHTTP::getXmlString(serverInfo, "ExternalIP");
    if (!remoteAddress.isEmpty()) {
        this->remoteAddress = NvAddress(remoteAddress, this->externalPort);
    }
    else {
        this->remoteAddress = NvAddress();
    }

    // Real Nvidia host software (GeForce Experience and RTX Experience) both use the 'Mjolnir'
    // codename in the state field and no version of Sunshine does. We can use this to bypass
    // some assumptions about Nvidia hardware that don't apply to Sunshine hosts.
    this->isNvidiaServerSoftware = NvHTTP::getXmlString(serverInfo, "state").contains("MJOLNIR");

    this->pairState = NvHTTP::getXmlString(serverInfo, "PairStatus") == "1" ?
                PS_PAIRED : PS_NOT_PAIRED;
    this->currentGameId = NvHTTP::getCurrentGame(serverInfo);
    this->appVersion = NvHTTP::getXmlString(serverInfo, "appversion");
    this->gfeVersion = NvHTTP::getXmlString(serverInfo, "GfeVersion");
    this->gpuModel = NvHTTP::getXmlString(serverInfo, "gputype");
    this->activeAddress = http.address();
    this->state = NvComputer::CS_ONLINE;
    this->pendingQuit = false;
    this->isSupportedServerVersion = CompatFetcher::isGfeVersionSupported(this->gfeVersion);
}

bool NvComputer::wake() const
{
    QByteArray wolPayload;

    {
        QReadLocker readLocker(&lock);

        if (state == NvComputer::CS_ONLINE) {
            qWarning() << name << "is already online";
            return true;
        }

        if (macAddress.isEmpty()) {
            qWarning() << name << "has no MAC address stored";
            return false;
        }

        // Create the WoL payload
        wolPayload.append(QByteArray::fromHex("FFFFFFFFFFFF"));
        for (int i = 0; i < 16; i++) {
            wolPayload.append(macAddress);
        }
        Q_ASSERT(wolPayload.size() == 102);
    }

    // Ports used as-is
    const quint16 STATIC_WOL_PORTS[] = {
        9, // Standard WOL port (privileged port)
        47009, // Port opened by Moonlight Internet Hosting Tool for WoL (non-privileged port)
    };

    // Ports offset by the HTTP base port for hosts using alternate ports
    const quint16 DYNAMIC_WOL_PORTS[] = {
        47998, 47999, 48000, 48002, 48010, // Ports opened by GFE
    };

    // Add the addresses that we know this host to be
    // and broadcast addresses for this link just in
    // case the host has timed out in ARP entries.
    QMap<QString, quint16> addressMap;
    QSet<quint16> basePortSet;
    for (const NvAddress& addr : uniqueAddresses()) {
        addressMap.insert(addr.address(), addr.port());
        basePortSet.insert(addr.port());
    }
    addressMap.insert("255.255.255.255", 0);

    // Try to broadcast on all available NICs
    for (const QNetworkInterface& nic : QNetworkInterface::allInterfaces()) {
        // Ensure the interface is up and skip the loopback adapter
        if ((nic.flags() & QNetworkInterface::IsUp) == 0 ||
                (nic.flags() & QNetworkInterface::IsLoopBack) != 0) {
            continue;
        }

        QHostAddress allNodesMulticast("FF02::1");
        for (const QNetworkAddressEntry& addr : nic.addressEntries()) {
            // Store the scope ID for this NIC if IPv6 is enabled
            if (!addr.ip().scopeId().isEmpty()) {
                allNodesMulticast.setScopeId(addr.ip().scopeId());
            }

            // Skip IPv6 which doesn't support broadcast
            if (!addr.broadcast().isNull()) {
                addressMap.insert(addr.broadcast().toString(), 0);
            }
        }

        if (!allNodesMulticast.scopeId().isEmpty()) {
            addressMap.insert(allNodesMulticast.toString(), 0);
        }
    }

    // Try all unique address strings or host names
    bool success = false;
    for (auto i = addressMap.constBegin(); i != addressMap.constEnd(); i++) {
        QHostAddress literalAddress;
        QList<QHostAddress> addressList;

        // If this is an IPv4/IPv6 literal, don't use QHostInfo::fromName() because that will
        // try to perform a reverse DNS lookup that leads to delays sending WoL packets.
        if (literalAddress.setAddress(i.key())) {
            addressList.append(literalAddress);
        }
        else {
            QHostInfo hostInfo = QHostInfo::fromName(i.key());
            if (hostInfo.error() != QHostInfo::NoError) {
                qWarning() << "Error resolving" << i.key() << ":" << hostInfo.errorString();
                continue;
            }

            addressList.append(hostInfo.addresses());
        }

        // Try all IP addresses that this string resolves to
        for (QHostAddress& address : addressList) {
            QUdpSocket sock;

            // Send to all static ports
            for (quint16 port : STATIC_WOL_PORTS) {
                if (sock.writeDatagram(wolPayload, address, port)) {
                    qInfo().nospace().noquote() << "Sent WoL packet to " << name << " via " << address.toString() << ":" << port;
                    success = true;
                }
                else {
                    qWarning() << "Send failed:" << sock.error();
                }
            }

            QList<quint16> basePorts;
            if (i.value() != 0) {
                // If we have a known base port for this address, use only that port
                basePorts.append(i.value());
            }
            else {
                // If this is a broadcast address without a known HTTP port, try all of them
                basePorts.append(basePortSet.values());
            }

            // Send to all dynamic ports using the HTTP port offset(s) for this address
            for (quint16 basePort : basePorts) {
                for (quint16 port : DYNAMIC_WOL_PORTS) {
                    port = (port - 47989) + basePort;

                    if (sock.writeDatagram(wolPayload, address, port)) {
                        qInfo().nospace().noquote() << "Sent WoL packet to " << name << " via " << address.toString() << ":" << port;
                        success = true;
                    }
                    else {
                        qWarning() << "Send failed:" << sock.error();
                    }
                }
            }
        }
    }

    return success;
}

NvComputer::ReachabilityType NvComputer::getActiveAddressReachability() const
{
    NvAddress copyOfActiveAddress;

    {
        QReadLocker readLocker(&lock);

        if (activeAddress.isNull()) {
            return ReachabilityType::RI_UNKNOWN;
        }

        // Grab a copy of the active address to avoid having to hold
        // the computer lock while doing socket operations
        copyOfActiveAddress = activeAddress;
    }

    QTcpSocket s;
    s.setProxy(QNetworkProxy::NoProxy);
    s.connectToHost(copyOfActiveAddress.address(), copyOfActiveAddress.port());
    if (s.waitForConnected(3000)) {
        Q_ASSERT(!s.localAddress().isNull());
        Q_ASSERT(!s.peerAddress().isNull());

        for (const QNetworkInterface& nic : QNetworkInterface::allInterfaces()) {
            // Ensure the interface is up
            if ((nic.flags() & QNetworkInterface::IsUp) == 0) {
                continue;
            }

            for (const QNetworkAddressEntry& addr : nic.addressEntries()) {
                if (addr.ip() == s.localAddress()) {
                    qInfo() << "Found matching interface:" << nic.humanReadableName() << nic.hardwareAddress() << nic.flags();

#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
                    qInfo() << "Interface Type:" << nic.type();
                    qInfo() << "Interface MTU:" << nic.maximumTransmissionUnit();

                    if (nic.type() == QNetworkInterface::Virtual ||
                            nic.type() == QNetworkInterface::Ppp) {
                        // Treat PPP and virtual interfaces as likely VPNs
                        return ReachabilityType::RI_VPN;
                    }

                    if (nic.maximumTransmissionUnit() != 0 && nic.maximumTransmissionUnit() < 1500) {
                        // Treat MTUs under 1500 as likely VPNs
                        return ReachabilityType::RI_VPN;
                    }
#endif

                    if (nic.flags() & QNetworkInterface::IsPointToPoint) {
                        // Treat point-to-point links as likely VPNs.
                        // This check detects OpenVPN on Unix-like OSes.
                        return ReachabilityType::RI_VPN;
                    }

#ifdef Q_OS_WINDOWS
                    if (nic.name().startsWith("iftype53_") || nic.name().startsWith("iftype131_")) {
                        // Match by NDIS interface type. These values are Microsoft's recommended values for VPN connections:
                        // https://learn.microsoft.com/en-US/troubleshoot/windows-client/networking/windows-connection-manager-disconnects-wlan#more-information
                        //
                        // The following VPNs use IF_TYPE_PROP_VIRTUAL under Windows:
                        //  - WireguardNT VPNs
                        //  - All WinTun-based VPNs (such as Slack Nebula)
                        //  - OpenVPN with tap-windows6
                        return ReachabilityType::RI_VPN;
                    }
#endif

                    if (nic.hardwareAddress().startsWith("00:FF", Qt::CaseInsensitive)) {
                        // OpenVPN TAP interfaces have a MAC address starting with 00:FF on Windows
                        return ReachabilityType::RI_VPN;
                    }

                    if (nic.humanReadableName().startsWith("ZeroTier")) {
                        // ZeroTier interfaces always start with "ZeroTier"
                        return ReachabilityType::RI_VPN;
                    }

                    if (nic.humanReadableName().contains("VPN")) {
                        // This one is just a final VPN heuristic if all else fails
                        return ReachabilityType::RI_VPN;
                    }

                    // Didn't meet any of our VPN heuristics. Let's see if the peer address is on-link.
                    Q_ASSERT(addr.prefixLength() >= 0);
                    if (addr.prefixLength() >= 0 && s.localAddress().isInSubnet(s.peerAddress(), addr.prefixLength())) {
                        return ReachabilityType::RI_LAN;
                    }

                    // Default to unknown if nothing else matched
                    return ReachabilityType::RI_UNKNOWN;
                }
            }
        }

        qWarning() << "No match found for address:" << s.localAddress();
        return ReachabilityType::RI_UNKNOWN;
    }
    else {
        // If we fail to connect, just pretend that it's not a VPN
        qWarning() << "Unable to check for reachability within 3 seconds";
        return ReachabilityType::RI_UNKNOWN;
    }
}

bool NvComputer::updateAppList(QVector<NvApp> newAppList) {
    if (appList == newAppList) {
        return false;
    }

    // Propagate client-side attributes to the new app list
    for (const NvApp& existingApp : appList) {
        for (NvApp& newApp : newAppList) {
            if (existingApp.id == newApp.id) {
                newApp.hidden = existingApp.hidden;
                newApp.directLaunch = existingApp.directLaunch;
            }
        }
    }

    appList = newAppList;
    sortAppList();
    return true;
}

QVector<NvAddress> NvComputer::uniqueAddresses() const
{
    QReadLocker readLocker(&lock);
    QVector<NvAddress> uniqueAddressList;

    // Start with addresses correctly ordered
    uniqueAddressList.append(activeAddress);
    uniqueAddressList.append(localAddress);
    uniqueAddressList.append(remoteAddress);
    uniqueAddressList.append(ipv6Address);
    uniqueAddressList.append(manualAddress);

    // Prune duplicates (always giving precedence to the first)
    for (int i = 0; i < uniqueAddressList.count(); i++) {
        if (uniqueAddressList[i].isNull()) {
            uniqueAddressList.remove(i);
            i--;
            continue;
        }
        for (int j = i + 1; j < uniqueAddressList.count(); j++) {
            if (uniqueAddressList[i] == uniqueAddressList[j]) {
                // Always remove the later occurrence
                uniqueAddressList.remove(j);
                j--;
            }
        }
    }

    // We must have at least 1 address
    Q_ASSERT(!uniqueAddressList.isEmpty());

    return uniqueAddressList;
}

bool NvComputer::update(const NvComputer& that)
{
    bool changed = false;

    // Lock us for write and them for read
    QWriteLocker thisLock(&this->lock);
    QReadLocker thatLock(&that.lock);

    // UUID may not change or we're talking to a new PC
    Q_ASSERT(this->uuid == that.uuid);

#define ASSIGN_IF_CHANGED(field)       \
    if (this->field != that.field) {   \
        this->field = that.field;      \
        changed = true;                \
    }

#define ASSIGN_IF_CHANGED_AND_NONEMPTY(field) \
    if (!that.field.isEmpty() &&              \
        this->field != that.field) {          \
        this->field = that.field;             \
        changed = true;                       \
    }

#define ASSIGN_IF_CHANGED_AND_NONNULL(field)  \
    if (!that.field.isNull() &&               \
        this->field != that.field) {          \
        this->field = that.field;             \
        changed = true;                       \
    }

    if (!hasCustomName) {
        // Only overwrite the name if it's not custom
        ASSIGN_IF_CHANGED(name);
    }
    ASSIGN_IF_CHANGED_AND_NONEMPTY(macAddress);
    ASSIGN_IF_CHANGED_AND_NONNULL(localAddress);
    ASSIGN_IF_CHANGED_AND_NONNULL(remoteAddress);
    ASSIGN_IF_CHANGED_AND_NONNULL(ipv6Address);
    ASSIGN_IF_CHANGED_AND_NONNULL(manualAddress);
    ASSIGN_IF_CHANGED(activeHttpsPort);
    ASSIGN_IF_CHANGED(externalPort);
    ASSIGN_IF_CHANGED(pairState);
    ASSIGN_IF_CHANGED(serverCodecModeSupport);
    ASSIGN_IF_CHANGED(currentGameId);
    ASSIGN_IF_CHANGED(activeAddress);
    ASSIGN_IF_CHANGED(state);
    ASSIGN_IF_CHANGED(gfeVersion);
    ASSIGN_IF_CHANGED(appVersion);
    ASSIGN_IF_CHANGED(isSupportedServerVersion);
    ASSIGN_IF_CHANGED(isNvidiaServerSoftware);
    ASSIGN_IF_CHANGED(maxLumaPixelsHEVC);
    ASSIGN_IF_CHANGED(gpuModel);
    ASSIGN_IF_CHANGED_AND_NONNULL(serverCert);
    ASSIGN_IF_CHANGED_AND_NONEMPTY(displayModes);

    if (!that.appList.isEmpty()) {
        // updateAppList() handles merging client-side attributes
        updateAppList(that.appList);
    }

    return changed;
}

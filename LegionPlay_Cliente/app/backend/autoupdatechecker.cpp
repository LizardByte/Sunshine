#include "autoupdatechecker.h"

#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

AutoUpdateChecker::AutoUpdateChecker(QObject *parent) :
    QObject(parent)
{
    m_Nam = new QNetworkAccessManager(this);

    // Never communicate over HTTP
    m_Nam->setStrictTransportSecurityEnabled(true);

    // Allow HTTP redirects
    m_Nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);

    connect(m_Nam, &QNetworkAccessManager::finished,
            this, &AutoUpdateChecker::handleUpdateCheckRequestFinished);

    QString currentVersion(VERSION_STR);
    qDebug() << "Current Moonlight version:" << currentVersion;
    parseStringToVersionQuad(currentVersion, m_CurrentVersionQuad);

    // Should at least have a 1.0-style version number
    Q_ASSERT(m_CurrentVersionQuad.count() > 1);
}

void AutoUpdateChecker::start()
{
    if (!m_Nam) {
        Q_ASSERT(m_Nam);
        return;
    }

#if defined(Q_OS_WIN32) || defined(Q_OS_DARWIN) || defined(STEAM_LINK) || defined(APP_IMAGE) // Only run update checker on platforms without auto-update
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0) && QT_VERSION < QT_VERSION_CHECK(5, 15, 1) && !defined(QT_NO_BEARERMANAGEMENT)
    // HACK: Set network accessibility to work around QTBUG-80947 (introduced in Qt 5.14.0 and fixed in Qt 5.15.1)
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    m_Nam->setNetworkAccessible(QNetworkAccessManager::Accessible);
    QT_WARNING_POP
#endif

    // We'll get a callback when this is finished
    QUrl url("https://moonlight-stream.org/updates/qt.json");
    QNetworkRequest request(url);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
#else
    request.setAttribute(QNetworkRequest::HTTP2AllowedAttribute, true);
#endif
    m_Nam->get(request);
#endif
}

void AutoUpdateChecker::parseStringToVersionQuad(QString& string, QVector<int>& version)
{
    QStringList list = string.split('.');
    for (const QString& component : list) {
        version.append(component.toInt());
    }
}

QString AutoUpdateChecker::getPlatform()
{
#if defined(STEAM_LINK)
    return QStringLiteral("steamlink");
#elif defined(APP_IMAGE)
    return QStringLiteral("appimage");
#elif defined(Q_OS_DARWIN) && QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Qt 6 changed this from 'osx' to 'macos'. Use the old one
    // to be consistent (and not require another entry in the manifest).
    return QStringLiteral("osx");
#else
    return QSysInfo::productType();
#endif
}

int AutoUpdateChecker::compareVersion(QVector<int>& version1, QVector<int>& version2) {
    for (int i = 0;; i++) {
        int v1Val = 0;
        int v2Val = 0;

        // Treat missing decimal places as 0
        if (i < version1.count()) {
            v1Val = version1[i];
        }
        if (i < version2.count()) {
            v2Val = version2[i];
        }
        if (i >= version1.count() && i >= version2.count()) {
            // Equal versions
            return 0;
        }

        if (v1Val < v2Val) {
            return -1;
        }
        else if (v1Val > v2Val) {
            return 1;
        }
    }
}

void AutoUpdateChecker::handleUpdateCheckRequestFinished(QNetworkReply* reply)
{
    Q_ASSERT(reply->isFinished());

    // Delete the QNetworkAccessManager to free resources and
    // prevent the bearer plugin from polling in the background.
    m_Nam->deleteLater();
    m_Nam = nullptr;

    if (reply->error() == QNetworkReply::NoError) {
        QTextStream stream(reply);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        stream.setEncoding(QStringConverter::Utf8);
#else
        stream.setCodec("UTF-8");
#endif

        // Read all data and queue the reply for deletion
        QString jsonString = stream.readAll();
        reply->deleteLater();

        QJsonParseError error;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonString.toUtf8(), &error);
        if (jsonDoc.isNull()) {
            qWarning() << "Update manifest malformed:" << error.errorString();
            return;
        }

        QJsonArray array = jsonDoc.array();
        if (array.isEmpty()) {
            qWarning() << "Update manifest doesn't contain an array";
            return;
        }

        for (QJsonValueRef updateEntry : array) {
            if (updateEntry.isObject()) {
                QJsonObject updateObj = updateEntry.toObject();
                if (!updateObj.contains("platform") ||
                        !updateObj.contains("arch") ||
                        !updateObj.contains("version") ||
                        !updateObj.contains("browser_url")) {
                    qWarning() << "Update manifest entry missing vital field";
                    continue;
                }

                if (!updateObj["platform"].isString() ||
                        !updateObj["arch"].isString() ||
                        !updateObj["version"].isString() ||
                        !updateObj["browser_url"].isString()) {
                    qWarning() << "Update manifest entry has unexpected vital field type";
                    continue;
                }

                if (updateObj["arch"] == QSysInfo::buildCpuArchitecture() &&
                        updateObj["platform"] == getPlatform()) {

                    // Check the kernel version minimum if one exists
                    if (updateObj.contains("kernel_version_at_least") && updateObj["kernel_version_at_least"].isString()) {
                        QVector<int> requiredVersionQuad;
                        QVector<int> actualVersionQuad;

                        QString requiredVersion = updateObj["kernel_version_at_least"].toString();
                        QString actualVersion = QSysInfo::kernelVersion();
                        parseStringToVersionQuad(requiredVersion, requiredVersionQuad);
                        parseStringToVersionQuad(actualVersion, actualVersionQuad);

                        if (compareVersion(actualVersionQuad, requiredVersionQuad) < 0) {
                            qDebug() << "Skipping manifest entry due to kernel version (" << actualVersion << "<" << requiredVersion << ")";
                            continue;
                        }
                    }

                    qDebug() << "Found update manifest match for current platform";

                    QString latestVersion = updateObj["version"].toString();
                    qDebug() << "Latest version of Moonlight for this platform is:" << latestVersion;

                    QVector<int> latestVersionQuad;
                    parseStringToVersionQuad(latestVersion, latestVersionQuad);

                    int res = compareVersion(m_CurrentVersionQuad, latestVersionQuad);
                    if (res < 0) {
                        // m_CurrentVersionQuad < latestVersionQuad
                        qDebug() << "Update available";
                        emit onUpdateAvailable(updateObj["version"].toString(),
                                               updateObj["browser_url"].toString());
                        return;
                    }
                    else if (res > 0) {
                        qDebug() << "Update manifest version lower than current version";
                        return;
                    }
                    else {
                        qDebug() << "Update manifest version equal to current version";
                        return;
                    }
                }
            }
            else {
                qWarning() << "Update manifest contained unrecognized entry:" << updateEntry.toString();
            }
        }

        qWarning() << "No entry in update manifest found for current platform:"
                   << QSysInfo::buildCpuArchitecture() << getPlatform() << QSysInfo::kernelVersion();
    }
    else {
        qWarning() << "Update checking failed with error:" << reply->error();
        reply->deleteLater();
    }
}

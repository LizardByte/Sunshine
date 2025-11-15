#include "nvapp.h"

#define SER_APPNAME "name"
#define SER_APPID "id"
#define SER_APPHDR "hdr"
#define SER_APPCOLLECTOR "appcollector"
#define SER_HIDDEN "hidden"
#define SER_DIRECTLAUNCH "directlaunch"

NvApp::NvApp(QSettings& settings)
{
    name = settings.value(SER_APPNAME).toString();
    id = settings.value(SER_APPID).toInt();
    hdrSupported = settings.value(SER_APPHDR).toBool();
    isAppCollectorGame = settings.value(SER_APPCOLLECTOR).toBool();
    hidden = settings.value(SER_HIDDEN).toBool();
    directLaunch = settings.value(SER_DIRECTLAUNCH).toBool();
}

void NvApp::serialize(QSettings& settings) const
{
    settings.setValue(SER_APPNAME, name);
    settings.setValue(SER_APPID, id);
    settings.setValue(SER_APPHDR, hdrSupported);
    settings.setValue(SER_APPCOLLECTOR, isAppCollectorGame);
    settings.setValue(SER_HIDDEN, hidden);
    settings.setValue(SER_DIRECTLAUNCH, directLaunch);
}

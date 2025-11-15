#include "richpresencemanager.h"

#include <QDebug>

RichPresenceManager::RichPresenceManager(StreamingPreferences& prefs, QString gameName)
    : m_DiscordActive(false)
{
#ifdef HAVE_DISCORD
    if (prefs.richPresence) {
        DiscordEventHandlers handlers = {};
        handlers.ready = discordReady;
        handlers.disconnected = discordDisconnected;
        handlers.errored = discordErrored;
        Discord_Initialize("594668102021677159", &handlers, 0, nullptr);
        m_DiscordActive = true;
    }

    if (m_DiscordActive) {
        QByteArray stateStr = (QString("Streaming ") + gameName).toUtf8();

        DiscordRichPresence discordPresence = {};
        discordPresence.state = stateStr.data();
        discordPresence.startTimestamp = time(nullptr);
        discordPresence.largeImageKey = "icon";
        Discord_UpdatePresence(&discordPresence);
    }
#else
    Q_UNUSED(prefs)
    Q_UNUSED(gameName)
#endif
}

RichPresenceManager::~RichPresenceManager()
{
#ifdef HAVE_DISCORD
    if (m_DiscordActive) {
        Discord_ClearPresence();
        Discord_Shutdown();
    }
#endif
}

void RichPresenceManager::runCallbacks()
{
#ifdef HAVE_DISCORD
    if (m_DiscordActive) {
        Discord_RunCallbacks();
    }
#endif
}

#ifdef HAVE_DISCORD
void RichPresenceManager::discordReady(const DiscordUser* request)
{
    qInfo() << "Discord integration ready for user:" << request->username;
}

void RichPresenceManager::discordDisconnected(int errorCode, const char *message)
{
    qInfo() << "Discord integration disconnected:" << errorCode << message;
}

void RichPresenceManager::discordErrored(int errorCode, const char *message)
{
    qWarning() << "Discord integration error:" << errorCode << message;
}
#endif

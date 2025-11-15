#pragma once

#include "settings/streamingpreferences.h"

#ifdef HAVE_DISCORD
#include <discord_rpc.h>
#endif

class RichPresenceManager
{
public:
    RichPresenceManager(StreamingPreferences& prefs, QString gameName);
    ~RichPresenceManager();

    void runCallbacks();

private:
#ifdef HAVE_DISCORD
    static void discordReady(const DiscordUser* request);
    static void discordDisconnected(int errorCode, const char* message);
    static void discordErrored(int errorCode, const char* message);
#endif

    bool m_DiscordActive;
};


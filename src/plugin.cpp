// Valve server plugin that re-logs the server onto Steam when steamclient gives
// up reconnecting, without restarting the server.

#include <stdint.h>
#include <string.h>
#include <time.h>

#define PLUGIN_VERSION "1.0.0"
#define PLUGIN_NAME "csgo-steam-reconnect v" PLUGIN_VERSION
#define PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#define LOG_PREFIX "csgo-steam-reconnect: "

// From libtier0.so and libsteam_api.so, which the engine has already loaded.
// Declared here instead of including the SDK's headers.
extern "C" {
// Prints to the server console, the console log (con_logfile) and RCON, unlike
// stdout.
void Msg(const char *format, ...) __attribute__((format(printf, 1, 2)));

struct ISteamGameServer;
int32_t SteamGameServer_GetHSteamPipe();
ISteamGameServer *SteamAPI_SteamGameServer_v014();
bool SteamAPI_ISteamGameServer_BLoggedOn(ISteamGameServer *self);
void SteamAPI_ISteamGameServer_LogOnAnonymous(ISteamGameServer *self);
uint64_t SteamAPI_ISteamGameServer_GetSteamID(ISteamGameServer *self);
}

namespace {

typedef void *(*CreateInterfaceFn)(const char *name, int *returnCode);

enum PluginResult {
    PLUGIN_CONTINUE = 0,
    PLUGIN_OVERRIDE,
    PLUGIN_STOP,
};

// The engine's game server interface, or null while the engine has no Steam
// session. The pipe is the handle steam_api clears on shutdown. The user
// handle isn't cleared, so checking it would let a lookup run against an
// unloaded steamclient.
ISteamGameServer *EngineGameServer() {
    if (SteamGameServer_GetHSteamPipe() == 0)
        return nullptr;
    return SteamAPI_SteamGameServer_v014();
}

// A normal reconnect takes about a minute, so the first re-logon waits longer
// than that. Later ones back off, since each one resets steamclient's own retry
// backoff, and on a stranded session also fires an assert that writes a
// minidump.
const int64_t kFirstDelay = 120;
const int64_t kMaxDelay = 600;

const unsigned kAccountTypeGameServer = 3; // a login token (GSLT) server
const unsigned kAccountTypeAnonGameServer = 4;

unsigned AccountType(uint64_t steamID) { return (steamID >> 52) & 0xF; }

int64_t Now() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

bool g_armed;
bool g_tokenServer;
int64_t g_lastCheck;
bool g_off;
int64_t g_offSince;
int64_t g_nextLogOn;
int64_t g_delay;
int g_logOns;

void ResetOffline() {
    g_off = false;
    g_delay = kFirstDelay;
    g_logOns = 0;
}

// Arms only for a server that logs on to Steam anonymously. The account type
// comes from the engine's logon call, before the server even connects, and
// can't change until a restart. An ID with no type yet means the engine hasn't
// made that call, so the decision waits, and a server that never makes it, like
// a LAN server, is left alone. So is a login token server, since logging it
// back in anonymously would drop its token.
void TryArm(ISteamGameServer *server) {
    if (g_armed || g_tokenServer)
        return;

    switch (AccountType(SteamAPI_ISteamGameServer_GetSteamID(server))) {
    case kAccountTypeAnonGameServer:
        g_armed = true;
        Msg(LOG_PREFIX "watching the server's steam connection\n");
        break;
    case kAccountTypeGameServer:
        g_tokenServer = true;
        Msg(LOG_PREFIX "this server logs in with a login token (GSLT) which "
                       "isn't supported, so doing nothing\n");
        break;
    default:
        break;
    }
}

void Check() {
    const int64_t now = Now();
    if (now == g_lastCheck)
        return;
    g_lastCheck = now;

    ISteamGameServer *server = EngineGameServer();
    if (!server)
        return;

    if (SteamAPI_ISteamGameServer_BLoggedOn(server)) {
        if (g_off) {
            Msg(LOG_PREFIX
                "back on steam after %lld seconds (logged back in %d time%s)\n",
                (long long)(now - g_offSince), g_logOns,
                g_logOns == 1 ? "" : "s");
        }
        ResetOffline();
        TryArm(server);
        return;
    }

    if (!g_armed)
        return;

    if (!g_off) {
        g_off = true;
        g_offSince = now;
        g_nextLogOn = now + g_delay;
        Msg(LOG_PREFIX "lost the steam connection, logging back in after %lld "
                       "seconds (unless it recovers)\n",
            (long long)g_delay);
        return;
    }

    if (now < g_nextLogOn)
        return;

    // Safe only because TryArm limits this to anonymous servers. The wrapper
    // sets the account type and clears the token before steamclient looks at
    // its own state, which would drop a login token server's token.
    SteamAPI_ISteamGameServer_LogOnAnonymous(server);
    ++g_logOns;
    Msg(LOG_PREFIX
        "logging back in to steam (attempt %d, offline for %lld seconds)\n",
        g_logOns, (long long)(now - g_offSince));

    g_delay = g_delay * 2 < kMaxDelay ? g_delay * 2 : kMaxDelay;
    g_nextLogOn = now + g_delay;
}

// Mirrors IServerPluginCallbacks (ISERVERPLUGINCALLBACKS004) from
// hl2sdk-csgo's public/engine/iserverplugin.h. Only the order of the virtuals
// matters, and the interface has no virtual destructor, so this matches the
// engine's layout without the SDK.
class Plugin {
public:
    virtual bool Load(CreateInterfaceFn, CreateInterfaceFn) {
        g_armed = false;
        g_tokenServer = false;
        g_lastCheck = 0;
        ResetOffline();

        Msg(LOG_PREFIX "v" PLUGIN_VERSION " loaded\n");
        // Arms even when the server is off Steam right now, since being loaded
        // by hand onto a stuck server to fix it is the point.
        if (ISteamGameServer *server = EngineGameServer())
            TryArm(server);
        return true;
    }

    virtual void Unload() {}
    virtual void Pause() {}
    virtual void UnPause() {}
    virtual const char *GetPluginDescription() { return PLUGIN_NAME; }
    virtual void LevelInit(const char *) {}
    virtual void ServerActivate(void *, int, int) {}
    virtual void GameFrame(bool) { Check(); }
    virtual void LevelShutdown() {}
    virtual void ClientActive(void *) {}
    virtual void ClientFullyConnect(void *) {}
    virtual void ClientDisconnect(void *) {}
    virtual void ClientPutInServer(void *, const char *) {}
    virtual void SetCommandClient(int) {}
    virtual void ClientSettingsChanged(void *) {}
    virtual PluginResult ClientConnect(bool *, void *, const char *,
                                       const char *, char *, int) {
        return PLUGIN_CONTINUE;
    }
    virtual PluginResult ClientCommand(void *, const void *) {
        return PLUGIN_CONTINUE;
    }
    virtual PluginResult NetworkIDValidated(const char *, const char *) {
        return PLUGIN_CONTINUE;
    }
    virtual void OnQueryCvarValueFinished(int, void *, int, const char *,
                                          const char *) {}
    virtual void OnEdictAllocated(void *) {}
    virtual void OnEdictFreed(const void *) {}
    virtual bool BNetworkCryptKeyCheckRequired(uint32_t, uint16_t, uint32_t,
                                               bool) {
        return false;
    }
    virtual bool BNetworkCryptKeyValidate(uint32_t, uint16_t, uint32_t, int,
                                          int, unsigned char *,
                                          unsigned char *) {
        return false;
    }
};

Plugin g_plugin;

} // namespace

PLUGIN_EXPORT void *CreateInterface(const char *name, int *returnCode) {
    if (name && strcmp(name, "ISERVERPLUGINCALLBACKS004") == 0) {
        if (returnCode)
            *returnCode = 0; // IFACE_OK
        return &g_plugin;
    }
    if (returnCode)
        *returnCode = 1; // IFACE_FAILED
    return nullptr;
}

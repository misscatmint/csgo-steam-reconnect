// Valve server plugin that re-logs the server onto Steam when steamclient gives
// up reconnecting, without restarting the server.

#include <stdint.h>
#include <string.h>
#include <time.h>

#define PLUGIN_VERSION "0.1.0"
#define PLUGIN_NAME "csgo-steam-reconnect v" PLUGIN_VERSION
#define PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#define LOG_PREFIX "csgo-steam-reconnect: "

// From libtier0.so and libsteam_api.so, which the engine has already loaded.
// Declared here instead of including the SDK's headers.
extern "C"
{
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

namespace
{

typedef void *(*CreateInterfaceFn)(const char *name, int *returnCode);

enum PluginResult
{
	PLUGIN_CONTINUE = 0,
	PLUGIN_OVERRIDE,
	PLUGIN_STOP,
};

// The engine's game server interface, or null while the engine has no Steam
// session. The pipe is the handle steam_api clears on shutdown. The user
// handle isn't cleared, so checking it would let a lookup run against an
// unloaded steamclient.
ISteamGameServer *EngineGameServer()
{
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

unsigned AccountType(uint64_t steamID)
{
	return (steamID >> 52) & 0xF;
}

// Prints the ID the way steamclient's logs and the console do, e.g. [A:1:123:45].
void LogSteamID(const char *message, uint64_t steamID)
{
	const unsigned type = AccountType(steamID);
	const char letter = type == kAccountTypeAnonGameServer ? 'A' : type == kAccountTypeGameServer ? 'G' : '?';
	Msg(LOG_PREFIX "%s [%c:%u:%u:%u]\n", message, letter,
		(unsigned)(steamID >> 56), (unsigned)(steamID & 0xFFFFFFFF), (unsigned)((steamID >> 32) & 0xFFFFF));
}

int64_t Now()
{
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec;
}

// Only acts on a server that is meant to be on Steam: one it has seen logged
// on, or one it was loaded onto at runtime while logged off. A server that
// never logs on, like a LAN server, is left alone. So is a login token server,
// since re-logging it on anonymously would drop its token.
bool g_armed;
bool g_tokenServer;
int64_t g_lastCheck;
bool g_off;
int64_t g_offSince;
int64_t g_nextLogOn;
int64_t g_delay;
int g_logOns;

void ResetOffline()
{
	g_off = false;
	g_delay = kFirstDelay;
	g_logOns = 0;
}

// The account type comes from the engine's logon call, before the server even
// connects, and can't change until a restart. An ID with no type yet means the
// engine hasn't called it, so the decision waits.
void TryArm(ISteamGameServer *server)
{
	if (g_armed || g_tokenServer)
		return;

	const uint64_t steamID = SteamAPI_ISteamGameServer_GetSteamID(server);
	switch (AccountType(steamID))
	{
	case kAccountTypeAnonGameServer:
		g_armed = true;
		LogSteamID("armed for anonymous server", steamID);
		break;
	case kAccountTypeGameServer:
		g_tokenServer = true;
		LogSteamID("not arming, this server uses a login token (GSLT), which isn't supported", steamID);
		break;
	default:
		break;
	}
}

void Check()
{
	const int64_t now = Now();
	if (now == g_lastCheck)
		return;
	g_lastCheck = now;

	ISteamGameServer *server = EngineGameServer();
	if (!server)
		return;

	if (SteamAPI_ISteamGameServer_BLoggedOn(server))
	{
		if (g_off)
		{
			Msg(LOG_PREFIX "back on Steam after %lld seconds, %d re-logon(s) by this plugin\n",
				(long long)(now - g_offSince), g_logOns);
		}
		ResetOffline();
		TryArm(server);
		return;
	}

	if (!g_armed)
		return;

	if (!g_off)
	{
		g_off = true;
		g_offSince = now;
		g_nextLogOn = now + g_delay;
		Msg(LOG_PREFIX "not logged on to Steam, re-logging on in %lld seconds if it stays that way\n",
			(long long)g_delay);
		LogSteamID("while logged off, the server's Steam ID reads", SteamAPI_ISteamGameServer_GetSteamID(server));
		return;
	}

	if (now < g_nextLogOn)
		return;

	// Safe only because TryArm limits this to anonymous servers. The wrapper
	// sets the account type and clears the token before steamclient looks at its
	// own state, which would drop a login token server's token.
	SteamAPI_ISteamGameServer_LogOnAnonymous(server);
	++g_logOns;
	Msg(LOG_PREFIX "re-logging on to Steam (attempt %d, off for %lld seconds)\n",
		g_logOns, (long long)(now - g_offSince));

	g_delay = g_delay * 2 < kMaxDelay ? g_delay * 2 : kMaxDelay;
	g_nextLogOn = now + g_delay;
}

// Mirrors IServerPluginCallbacks (ISERVERPLUGINCALLBACKS004) from
// hl2sdk-csgo's public/engine/iserverplugin.h. Only the order of the virtuals
// matters, and the interface has no virtual destructor, so this matches the
// engine's layout without the SDK.
class Plugin
{
public:
	virtual bool Load(CreateInterfaceFn, CreateInterfaceFn)
	{
		g_armed = false;
		g_tokenServer = false;
		g_lastCheck = 0;
		ResetOffline();

		ISteamGameServer *server = EngineGameServer();
		if (!server)
		{
			Msg(LOG_PREFIX "v" PLUGIN_VERSION " loaded, the engine hasn't set up Steam yet\n");
		}
		else if (SteamAPI_ISteamGameServer_BLoggedOn(server))
		{
			Msg(LOG_PREFIX "v" PLUGIN_VERSION " loaded, the server is logged on to Steam\n");
			TryArm(server);
		}
		else
		{
			// Loaded by hand onto a server that's off Steam, presumably to fix it.
			Msg(LOG_PREFIX "v" PLUGIN_VERSION " loaded, the server is NOT logged on to Steam\n");
			TryArm(server);
		}
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
	virtual PluginResult ClientConnect(bool *, void *, const char *, const char *, char *, int) { return PLUGIN_CONTINUE; }
	virtual PluginResult ClientCommand(void *, const void *) { return PLUGIN_CONTINUE; }
	virtual PluginResult NetworkIDValidated(const char *, const char *) { return PLUGIN_CONTINUE; }
	virtual void OnQueryCvarValueFinished(int, void *, int, const char *, const char *) {}
	virtual void OnEdictAllocated(void *) {}
	virtual void OnEdictFreed(const void *) {}
	virtual bool BNetworkCryptKeyCheckRequired(uint32_t, uint16_t, uint32_t, bool) { return false; }
	virtual bool BNetworkCryptKeyValidate(uint32_t, uint16_t, uint32_t, int, int, unsigned char *, unsigned char *) { return false; }
};

Plugin g_plugin;

} // namespace

PLUGIN_EXPORT void *CreateInterface(const char *name, int *returnCode)
{
	if (name && strcmp(name, "ISERVERPLUGINCALLBACKS004") == 0)
	{
		if (returnCode)
			*returnCode = 0; // IFACE_OK
		return &g_plugin;
	}
	if (returnCode)
		*returnCode = 1; // IFACE_FAILED
	return nullptr;
}

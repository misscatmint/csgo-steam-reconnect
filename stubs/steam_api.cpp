// Link-time stand-in for the engine's libsteam_api.so, so the plugin records a
// dependency on it. Only the names matter. The server's real copy is used.

#include <stdint.h>

extern "C" {
int32_t SteamGameServer_GetHSteamPipe() { return 0; }
void *SteamAPI_SteamGameServer_v014() { return 0; }
bool SteamAPI_ISteamGameServer_BLoggedOn(void *) { return false; }
void SteamAPI_ISteamGameServer_LogOnAnonymous(void *) {}
uint64_t SteamAPI_ISteamGameServer_GetSteamID(void *) { return 0; }
}

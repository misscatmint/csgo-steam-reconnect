// Link-time stand-in for the engine's libtier0.so, so the plugin records a
// dependency on it. Only the names matter. The server's real copy is used.
extern "C" void Msg(const char *, ...) {}

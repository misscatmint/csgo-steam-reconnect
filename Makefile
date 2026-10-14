# Extra flags can be added with make CXXFLAGS=..., which these don't depend on.
PLUGIN_CXXFLAGS = -m32 -O2 -fPIC -fvisibility=hidden -fno-exceptions -fno-rtti \
                  -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wconversion -Wsign-conversion
# --as-needed keeps toolchains that don't default to it (like clang) from adding
# libstdc++ and libgcc_s, which the plugin doesn't use.
LDFLAGS = -m32 -shared -Wl,--no-undefined -Wl,--as-needed -Lbuild/stubs
STUBS = build/stubs/libtier0.so build/stubs/libsteam_api.so

build/csgo-steam-reconnect.so: src/plugin.cpp $(STUBS) Makefile tools/check-so.sh
	$(CXX) $(PLUGIN_CXXFLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ $< -l:libtier0.so -l:libsteam_api.so
	tools/check-so.sh $@ || { rm -f $@; exit 1; }

# Stand-ins for the engine's libraries, linked against only for their names.
build/stubs/lib%.so: stubs/%.cpp Makefile
	mkdir -p build/stubs
	$(CXX) -m32 -shared -fPIC -o $@ $<

clean:
	rm -rf build dist

.PHONY: clean

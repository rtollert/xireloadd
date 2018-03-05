
OPT_DEBUG_OPTS=-g -O2
PKG_CONFIG_OPTS=$(shell pkg-config --cflags --libs x11 xi)

xidmon: xidmon.cpp
	g++ -Wall $(OPT_DEBUG_OPTS) xidmon.cpp -o xidmon $(PKG_CONFIG_OPTS)

clean:
	rm xidmon

.PHONY: clean

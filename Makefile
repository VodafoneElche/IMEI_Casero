# Compilación cruzada para Windows x64 con MinGW-w64 (Linux o MSYS2).
#   make            -> dist/ComprobadorIMEI.exe
#   make installer  -> dist/ComprobadorIMEI-Setup.exe (necesita makensis)
#   make selftest   -> build/selftest.exe (pruebas del núcleo)

VERSION := $(strip $(shell cat VERSION))
VER_PARTS := $(subst ., ,$(VERSION))
CC ?= x86_64-w64-mingw32-gcc
ifeq ($(origin CC),default)
CC := x86_64-w64-mingw32-gcc
endif
WINDRES ?= x86_64-w64-mingw32-windres
PYTHON ?= python3

CFLAGS := -O2 -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
          -municode -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
          -DAPP_VERSION=\"$(VERSION)\"
LIBS := -lwinhttp -lcomctl32 -lcomdlg32 -lshell32 -luxtheme -ldwmapi -lmsimg32 -lgdi32 -luser32 -lole32
LDFLAGS := -municode -static -static-libgcc -s

CORE := src/common.c src/imei.c src/tacdb.c src/store.c src/net.c src/wiki.c src/makers.c src/update.c vendor/cjson/cJSON.c
APP := src/main.c src/ui.c src/settings.c src/logo.c $(CORE)
HEADERS := $(wildcard src/*.h) vendor/cjson/cJSON.h

all: dist/ComprobadorIMEI.exe

build/tac.bin: data/tac_full.csv tools/build_tacdb.py
	@mkdir -p build
	$(PYTHON) tools/build_tacdb.py $@

build/version.h: VERSION
	@mkdir -p build
	printf '#define VER_MAJOR %s\n#define VER_MINOR %s\n#define VER_PATCH %s\n#define VER_STRING "%s"\n' \
		$(word 1,$(VER_PARTS)) $(word 2,$(VER_PARTS)) $(word 3,$(VER_PARTS)) $(VERSION) > $@

build/app.res.o: src/app.rc src/app.manifest src/resource.h build/version.h build/tac.bin assets/icon.ico assets/fonts/*.ttf
	$(WINDRES) -I build -I src -i src/app.rc -o $@

dist/ComprobadorIMEI.exe: $(APP) $(HEADERS) build/app.res.o
	@mkdir -p dist
	$(CC) $(CFLAGS) -mwindows -o $@ $(APP) build/app.res.o $(LDFLAGS) $(LIBS)

build/selftest.exe: tests/selftest.c $(CORE) $(HEADERS) build/app.res.o
	$(CC) $(CFLAGS) -mconsole -o $@ tests/selftest.c $(CORE) build/app.res.o $(LDFLAGS) $(LIBS)

selftest: build/selftest.exe

installer: dist/ComprobadorIMEI.exe installer/installer.nsi
	makensis -V2 -DVERSION=$(VERSION) installer/installer.nsi

clean:
	rm -rf build dist

.PHONY: all selftest installer clean

# Installs Addoner for the current user under ~/.local (no sudo), so it shows
# up in the Omarchy app launcher. bin/install builds a system package instead.
# The qmake build itself lives in build/, so this file doesn't clash with it.

PREFIX ?= $(HOME)/.local
BIN     = $(PREFIX)/bin
APPS    = $(PREFIX)/share/applications
ICONS   = $(PREFIX)/share/icons/hicolor/scalable/apps

.PHONY: all build test install uninstall clean

all: build

build:
	bin/build

test:
	bin/test

install: build
	install -Dm755 build/addoner $(BIN)/addoner
	install -Dm644 pkgbuild/addoner.svg $(ICONS)/addoner.svg
	# Exec points at the full path so the launcher finds it even when
	# ~/.local/bin isn't on the session PATH.
	install -d $(APPS)
	sed 's|^Exec=addoner|Exec=$(BIN)/addoner|' pkgbuild/addoner.desktop > $(APPS)/addoner.desktop
	@update-desktop-database -q $(APPS) 2>/dev/null || true
	@gtk-update-icon-cache -q -t $(PREFIX)/share/icons/hicolor 2>/dev/null || true
	@echo "Installed $(BIN)/addoner. Open it from the launcher (Super + Space)."

uninstall:
	rm -f $(BIN)/addoner $(APPS)/addoner.desktop $(ICONS)/addoner.svg
	@update-desktop-database -q $(APPS) 2>/dev/null || true
	@echo "Removed Addoner. Settings stay in ~/.config/addoner."

clean:
	rm -rf build

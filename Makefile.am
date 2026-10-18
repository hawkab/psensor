SUBDIRS = doc \
	po \
	src \
	icons/hicolor/scalable \
	icons/hicolor/14x14 \
	icons/hicolor/16x16 \
	icons/hicolor/22x22 \
	icons/hicolor/24x24 \
	icons/hicolor/32x32 \
	icons/hicolor/48x48 \
	icons/hicolor/64x64 \
	icons/hicolor/128x128 \
	icons/hicolor/256x256 \
	icons/ubuntu-mono-dark/status/22 \
	icons/ubuntu-mono-light/status/22 \
	icons/ubuntu-mono-dark/apps/22 \
	www \
	analysis \
	tests

dist_doc_DATA = AUTHORS \
	COPYING \
	INSTALL \
	NEWS \
	NEWS.html \
	README \
	README.html

desktopdir = $(datadir)/applications
desktop_DATA = psensor.desktop

EXTRA_DIST = $(desktop_DATA) $(autostart_DATA) README.md \
	scripts/install-user.py scripts/install-it87.sh \
	doc/analysis/README.ru.md doc/analysis/PERFORMANCE.md doc/analysis/FANS.md \
	tests/analysis/run.py tests/analysis/ui_fixture.py \
	tests/analysis/test-model.py tests/analysis/test-history-store.py \
	tests/analysis/test-lazy-ui.py tests/analysis/test-lifecycle.py \
	tests/analysis/test-buffer-flush.py tests/analysis/test-actions.c \
	tests/analysis/test-action-tab.c tests/analysis/test-notification.c \
	tests/analysis/test-notification.py tests/analysis/test-tray-allocation.c \
	tests/analysis/test-native-refresh.c tests/analysis/test-install-user.py \
	tests/analysis/test-cooling.c tests/analysis/test-cooling-runtime.c \
	tests/analysis/test-cooling-config.c \
	tests/analysis/test-tray-color.c tests/analysis/test-tray-tab.c \
	tests/analysis/test-tray-dbus.c tests/analysis/test-tray-dbus.py \
	tests/analysis/test-process-control.py tests/analysis/test-process-menu.py

if ASCIIDOCTOR
README.html: README
	asciidoctor -n README

NEWS.html: NEWS
	asciidoctor NEWS
endif

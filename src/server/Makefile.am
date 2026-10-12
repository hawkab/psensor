bin_PROGRAMS =  psensor-server
psensor_server_SOURCES = server.c server.h

AM_CPPFLAGS = -Wall -Werror -DDEFAULT_WWW_DIR=\""$(pkgdatadir)/www"\"\
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/lib \
	$(SENSORS_CFLAGS)\
	$(JSON_CFLAGS)\
	$(LIBMICROHTTPD_CFLAGS)

DEFS = -DLOCALEDIR=\"$(localedir)\" @DEFS@

LIBS = \
	../lib/libpsensor.a \
	$(SENSORS_LIBS) \
	$(JSON_LIBS) \
	$(LIBMICROHTTPD_LIBS) \
	$(PTHREAD_LIBS)

if GTOP
AM_CPPFLAGS += $(GTOP_CFLAGS)
LIBS += $(GTOP_LIBS) 
AM_LDFLAGS = -Wl,--as-needed
psensor_server_SOURCES += sysinfo.h sysinfo.c
endif

if ATASMART
AM_CPPFLAGS += $(ATASMART_CFLAGS)
LIBS += $(ATASMART_LIBS)
endif

if HELP2MAN
dist_man_MANS = psensor-server.1
psensor-server.1: psensor-server$(EXEEXT) $(srcdir)/description.txt
	help2man --include=$(srcdir)/description.txt -N --name="Temperature and system monitoring Web server" --output=psensor-server.1 ./psensor-server$(EXEEXT)
endif

EXTRA_DIST = description.txt
DISTCLEANFILES = psensor-server.1

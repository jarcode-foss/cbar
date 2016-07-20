# This snippet has been shmelessly stol^Hborrowed from thestinger's repose Makefile
VERSION = 1.1
GIT_DESC=$(shell test -d .git && git describe --always 2>/dev/null)

ifneq "$(GIT_DESC)" ""
	VERSION=$(GIT_DESC)
endif

CC	?= gcc
CFLAGS += -Wall -std=c99 -Os -DVERSION="\"$(VERSION)\"" -I/usr/include/freetype2
LDFLAGS += -lm -lsensors -lxcb -lxcb-xinerama -lxcb-randr -lX11 -lX11-xcb -lXft -lfreetype -lz -lfontconfig
CFDEBUG = -g3 -pedantic -Wall -Wunused-parameter -Wlong-long \
          -Wsign-conversion -Wconversion -Wimplicit-function-declaration

EXEC = cbar
SRCS = cbar.c render.c
OBJS = ${SRCS:.c=.o}

PREFIX?=/usr
BINDIR=${PREFIX}/bin

all: ${EXEC}

.c.o:
	${CC} ${CFLAGS} -o $@ -c $<

${EXEC}: ${OBJS}
	${CC} -o ${EXEC} ${OBJS} ${LDFLAGS}

debug: ${EXEC}
debug: CC += ${CFDEBUG}

clean:
	rm -f ./*.o ./*.1
	rm -f ./${EXEC}

install: cbar doc
	install -D -m 755 cbar ${DESTDIR}${BINDIR}/cbar
	install -D -m 644 cbar.1 ${DESTDIR}${PREFIX}/share/man/man1/cbar.1

uninstall:
	rm -f ${DESTDIR}${BINDIR}/cbar
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/cbar.1

.PHONY: all debug clean install

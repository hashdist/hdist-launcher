INSTALL = install
PREFIX = /usr/local
BINS = launcher

all: launcher

launcher: launcher.c
	${CC} -g -Os -Wall -Wno-unused-function -o launcher launcher.c
	strip launcher

install: ${BINS}
	$(INSTALL) -dm0755 "${DESTDIR}${PREFIX}/bin/"
	$(INSTALL) -m0755 ${BINS} "${DESTDIR}${PREFIX}/bin/"

clean:
	rm -f launcher launcher.o

.PHONY: all clean install

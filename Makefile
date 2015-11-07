include Makefile.flags

TARGET = driver
OBJS = com.o hal.o HALFS.o logger.o
VERSION = $(shell git log | head -1 | cut -d ' ' -f 2)

all: ${TARGET}

${TARGET}: ${TARGET}.o ${OBJS}
	${CC} -o $@ $^ ${LDFLAGS}

%.o: %.c version.h
	${CC} ${DEFINES} ${WARNINGS} ${CFLAGS} ${CPPFLAGS} -c -o $@ $<

version.h: version.h.tpl .git
	sed 's/{{version}}/${VERSION}/' < $< > $@

.PHONY: clean mrproper tests
clean:
	rm -f *.o version.h
	+make -C tests clean

mrproper:
	rm -f ${TARGET}

tests:
	+make -C $@

include Makefile.flags

TARGET = driver
OBJS = com.o hal.o HALFS.o logger.o
VERSION = $(shell git log | head -1 | cut -d ' ' -f 2)
CPPFLAGS += -DHAL_DRIVER_VERSION=\"${VERSION}\"

all: ${TARGET}

${TARGET}: ${TARGET}.o ${OBJS}
	${CC} -o $@ $^ ${LDFLAGS}

%.o: %.c
	${CC} ${DEFINES} ${WARNINGS} ${CFLAGS} ${CPPFLAGS} -c -o $@ $<

.PHONY: clean mrproper tests
clean:
	rm -f *.o version.h
	+make -C tests clean

mrproper: clean
	rm -f ${TARGET}

tests:
	+make -C $@

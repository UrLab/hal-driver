ALL_TESTS_OK: test_HALFS.ok test_HALMsg.ok
	touch $@

include ../Makefile.flags

%.ok: %.test
	./$< -v && touch $@

test_HALFS.test: test_HALFS.c ../HALFS.c
	gcc ${DEFINES} ${CFLAGS} ${LDFLAGS} $^ -o $@

test_HALMsg.test: test_HALMsg.c ../HALMsg.c
	gcc ${DEFINES} ${CFLAGS} ${LDFLAGS} $^ -o $@

clean:
	rm -f *.ok ALL_TESTS_OK *.test

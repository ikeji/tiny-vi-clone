.PHONY: all vi vi-quiet host hello test clean table

all: vi host

host: src/vi.c src/tuttable.h
	./build/build-host.sh

# Regenerate src/tuttable.h from ibus-tutcode's rules.
table:
	python3 build/gen_tut_table.py

vi: src/vi.c src/tuttable.h
	./build/build.sh src/vi.c vi.exe

# "Quiet" build: no cursor position or TUT stroke buffer in the status
# line. The status line becomes cacheable so most keystrokes skip the
# status redraw entirely -- useful on very slow real hardware.
vi-quiet: src/vi.c src/tuttable.h
	VI_DEFINES=-DVI_QUIET_STATUS ./build/build.sh src/vi.c vi.exe

hello: src/hello.c
	./build/build.sh src/hello.c hello.exe

vitest.exe: src/vitest.c src/vi.c
	./build/build.sh src/vitest.c vitest.exe

test: vitest.exe
	@./test/run.sh vitest.exe | tr -d '\r' | tee work/run/test-output.txt
	@grep -q '^SUMMARY pass=.* fail=0$$' work/run/test-output.txt || (echo "TESTS FAILED" && exit 1)
	@echo "All tests passed."

clean:
	rm -rf work
	rm -f vi.exe hello.exe vitest.exe vi-host

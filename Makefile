CC = gcc
CFLAGS = -Wall -Wextra -Werror -g
SANFLAGS = -O1 -fno-omit-frame-pointer
SRCS = http_server.c http.c connection_queue.c
port = 8000

.PHONY: all sanitize test test-setup clean clean-tests zip

all: http_server

http_server: http_server.c http.o connection_queue.o
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

http.o: http.c http.h
	$(CC) $(CFLAGS) -c http.c

connection_queue.o: connection_queue.c connection_queue.h
	$(CC) $(CFLAGS) -c connection_queue.c

# Linux-only LD_PRELOAD shim used by the concurrency test
concurrent_open.so: concurrent_open.c
	$(CC) $(CFLAGS) -shared -fpic -o $@ $^ -ldl

# ASan and TSan can't be combined, so build a separate binary for each
sanitize: http_server_asan http_server_tsan

http_server_asan: $(SRCS) http.h connection_queue.h
	$(CC) $(CFLAGS) $(SANFLAGS) -fsanitize=address,undefined -o $@ $(SRCS) -lpthread

http_server_tsan: $(SRCS) http.h connection_queue.h
	$(CC) $(CFLAGS) $(SANFLAGS) -fsanitize=thread -o $@ $(SRCS) -lpthread

test-setup:
	@chmod u+x testius
	@rm -rf downloaded_files

test: test-setup http_server clean-tests concurrent_open.so
	PORT=$(port) ./testius test_cases/tests.json -v

clean:
	rm -rf *.o *.dSYM concurrent_open.so http_server http_server_asan http_server_tsan

clean-tests:
	rm -rf test_results
	rm -rf downloaded_files

zip:
	@echo "ERROR: You cannot run 'make zip' from the part2 subdirectory. Change to the main proj4-code directory and run 'make zip' there."

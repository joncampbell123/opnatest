vpath %.c ../src

CC=i686-w64-mingw32-gcc

TARGET=opnatest.exe
OBJS=main.o

SDLCONFIG=/usr/i686-w64-mingw32/bin/sdl2-config
CFLAGS=-Wall -Wextra -O3 $(shell $(SDLCONFIG) --cflags)
LDFLAGS=-static -s
LIBS=$(shell $(SDLCONFIG) --static-libs)

$(TARGET):	$(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)

clean:
	rm -f $(TARGET) $(OBJS)


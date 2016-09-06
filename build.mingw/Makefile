vpath %.c ../src

CC=i686-w64-mingw32-gcc

TARGET=opnatest.exe
OBJS=main.o opnafm.o

SDLDIR=/home/tak/src/SDL2-2.0.4

CFLAGS=-Wall -Wextra -O3 -I$(SDLDIR)/i686-w64-mingw32/include/SDL2 -Dmain=SDL_main
LDFLAGS=-static -s
LIBS=-L$(SDLDIR)/i686-w64-mingw32/lib -lmingw32 -lSDL2main -lSDL2.dll -mwindows

$(TARGET):	$(OBJS) SDL2.dll
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)

SDL2.dll:
	cp $(SDLDIR)/i686-w64-mingw32/bin/SDL2.dll .

clean:
	rm -f $(TARGET) $(OBJS)


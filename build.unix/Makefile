vpath %.c ../src

TARGET=opnatest
OBJS=main.o opnafm.o

SDLCONFIG=sdl2-config
CFLAGS=-Wall -Wextra -O3 $(shell $(SDLCONFIG) --cflags)
LDFLAGS=
LIBS=$(shell $(SDLCONFIG) --static-libs)

$(TARGET):	$(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)

clean:
	rm -f $(TARGET) $(OBJS)


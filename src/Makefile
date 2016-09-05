TARGET=opnatest
OBJS=main.o

SDLCONFIG=sdl2-config
CFLAGS=-Wall -Wextra -Og -g $(shell $(SDLCONFIG) --cflags)
LDFLAGS=
LIBS=$(shell $(SDLCONFIG) --libs)

$(TARGET):	$(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)

clean:
	rm -f $(TARGET) $(OBJS)


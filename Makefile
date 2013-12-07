
GIMPTOOL=gimptool-2.0

CC=gcc
CFLAGS+=-pipe -O2 -g -Wall $(shell pkg-config --cflags gtk+-2.0 gtkglext-1.0 gimp-2.0)
LDFLAGS+=

OS=$(shell uname -s)
ifeq (,$(findstring Windows,$(OS)))
EXT=
else
EXT=.exe
endif

TARGET=normalmap$(EXT)

SRCS=normalmap.c preview3d.c scale.c
OBJS=$(SRCS:.c=.o)

LIBS=$(shell pkg-config --libs gtk+-2.0 gtkglext-1.0 gimp-2.0 gimpui-2.0) \
-L/usr/X11R6/lib -lGLEW -lm

ifdef VERBOSE
Q=
else
Q=@
endif

all: $(TARGET)

$(TARGET): $(OBJS)
	$(Q)echo "[LD]\t$@"
	$(Q)$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $(TARGET)
		 
clean:
	rm -f *.o $(TARGET)
	
install: all
	$(GIMPTOOL) --install-bin $(TARGET)
		
.c.o:
	$(Q)echo "[CC]\t$<"
	$(Q)$(CC) -c $(CFLAGS) -o $@ $<
	  
normalmap.o: normalmap.c scale.h preview3d.h
preview3d.o: preview3d.c scale.h  objects/cube.h objects/quad.h \
objects/sphere.h objects/torus.h objects/teapot.h pixmaps/object.xpm \
pixmaps/light.xpm pixmaps/scene.xpm pixmaps/full.xpm
scale.o: scale.c scale.h

ifdef WIN32
-include Makefile.mingw32
else ifdef WIN64
-include Makefile.mingw64
endif


GIMPTOOL=gimptool-2.0

CC=gcc
CFLAGS=-O3 -Wall `pkg-config --cflags gtk+-2.0 gtkglext-1.0 gimp-2.0`
LD=gcc
LDFLAGS=

ifdef WIN32
CFLAGS+=-DWIN32
LDFLAGS+=-mwindows
endif

TARGET=normalmap

OBJS=normalmap.o preview3d.o scale.o

LIBS=`pkg-config --libs gtk+-2.0 gtkglext-1.0 gimp-2.0 gimpui-2.0`
ifdef WIN32
LIBS+=-lglew32
else
LIBS+=-L/usr/X11R6/lib -lGLEW
endif

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) $(LIBS) -o $(TARGET)
		 
clean:
	rm -f *.o $(TARGET)
	
install: all
	$(GIMPTOOL) --install-bin $(TARGET)
		
.c.o:
	$(CC) -c $(CFLAGS) $<
	  
normalmap.o: normalmap.c scale.h preview3d.h Makefile
preview3d.o: preview3d.c scale.h  objects/sphere.h objects/torus.h \
objects/teapot.h pixmaps/object.xpm pixmaps/light.xpm pixmaps/scene.xpm \
pixmaps/full.xpm Makefile
scale.o: scale.c Makefile


GIMPTOOL=gimptool-2.1

CC=gcc
CFLAGS=-O3 -Wall `$(GIMPTOOL) --cflags` `pkg-config --cflags gtkglext-1.0`
LD=gcc
LDFLAGS=

TARGET=normalmap

OBJS=gimpoldpreview.o normalmap.o preview3d.o

LIBS=`$(GIMPTOOL) --libs` `pkg-config --libs gtkglext-1.0`

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) $(LIBS) -o $(TARGET)
		 
clean:
	rm -f *.o $(TARGET)
	
install: all
	$(GIMPTOOL) --install-bin $(TARGET)
		
.c.o:
	$(CC) -c $(CFLAGS) $<
	  
normalmap.o: normalmap.c preview3d.h Makefile
preview3d.o: preview3d.c preview3d.h Makefile

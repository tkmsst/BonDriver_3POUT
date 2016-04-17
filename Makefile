CXX = g++
CC = gcc
CPPFLAGS = -DLINUX -DNDEBUG
CXXFLAGS = -Wall -pthread -O2
CFLAGS = -O2 -Wall
SOFLAGS = -shared

OBJS = BonDriver_3POUT.o message.o osdepend.o usbdevfile.o usbops.o tsthread.o em287x.o em287x_usb.o tc90522.o mxl136.o tda20142.o
LIBS = -lpthread -ldl
TARGET = BonDriver_3POUT.so

ifdef B25
LIBS += -laribb25
endif
ifeq ($(shell getconf LONG_BIT),32)
CFLAGS	+= -D_FILE_OFFSET_BITS=64
endif

all: $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(SOFLAGS) $(CXXFLAGS) -o $@ $^ $(LIBS)

depend:
	$(CC) -MM $(OBJS:.o=.c) > Makefile.dep

-include Makefile.dep

BonDriver_3POUT.o :
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<

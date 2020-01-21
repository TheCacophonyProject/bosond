CXX = g++
CC = $(CXX)

EXEC = bosond

SRC = bosond.cpp
OBJS := $(SRC:.cpp=.o)

SDK_SRC = $(wildcard boson_sdk/*.c)
SDK_OBJS := $(SDK_SRC:.c=.o)

# C++ compiler flags
DEBUG_LEVEL = -g
EXTRA_CCFLAGS = -Wall
CXXFLAGS        = $(DEBUG_LEVEL) $(EXTRA_CCFLAGS)
CCFLAGS         = $(CXXFLAGS)
CPPFLAGS        = -I /usr/include/libusb-1.0 -I boson_sdk
LDLIBS          = -lusb-1.0

# C compiler flags (for boson_sdk). The SDK code isn't that clean so
# warnings are suppressed.
CFLAGS = $(DEBUG_LEVEL) -fpermissive -w

all: $(EXEC)

$(EXEC): $(OBJS) $(SDK_OBJS)

.PHONY: clean
clean:
	rm -f ${EXEC} ${OBJS} $(SDK_OBJS)

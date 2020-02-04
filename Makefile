EXEC      = bosond
SRC_FILES = bosond.cpp

CXX = g++
CC = $(CXX)

# What flags should be passed to the compiler

DEBUG_LEVEL     = -g
EXTRA_CCFLAGS   = -Wall
CXXFLAGS        = $(DEBUG_LEVEL) $(EXTRA_CCFLAGS)
CCFLAGS         = $(CXXFLAGS)
O_FILES         = $(SRC_FILES:%.cpp=%.o)

all: $(EXEC)

$(EXEC): $(O_FILES)

.PHONY: clean
clean:
	rm -f ${EXEC} ${O_FILES}

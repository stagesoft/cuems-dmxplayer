####### USAGE
# make - buidls with debug options
# make release - builds release options
# make clean - removes object files

#Compiler, compiler flags and linker flags
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17

# User preprocessor defines
CXXFLAGS += 

LDFLAGS =  -fsanitize=address
LBLIBS = -lrtmidi -lpthread -lxerces-c -lstdc++fs -lola -lolacommon\
			./oscreceiver_class/oscpack/osc/OscReceivedElements.o \
			./oscreceiver_class/oscpack/ip/posix/UdpSocket.o

TARGET := dmxplayer
SRC := $(wildcard *.cpp) \
			$(wildcard ./oscreceiver_class/*.cpp) \
			$(wildcard ./mtcreceiver_class/*.cpp) \
			$(wildcard ./sysqlogger_class/*.cpp)

INC := $(wildcard *.h)
OBJ := $(SRC:.cpp=.o)

.PHONY: clean clear

all: debug

release: CXXFLAGS += -O3
release: target

debug: CXXFLAGS += -g -Og
debug: target

target: $(TARGET)

$(TARGET): $(OBJ) oscpack
	$(CXX) $(OBJ) -o $@ $(LBLIBS)

oscpack:
	@echo Checking oscpack objects to be built
	@cd oscreceiver_class/oscpack/ 1> /dev/null && make -i 1> /dev/null
	@cd ../.. 1> /dev/null

wipe:
	@clear

clean:
	rm -rf $(OBJ)
CXXFILES := \
    source/serial/src/serial.cc \
    source/serial/src/impl/unix.cc \
    source/serial/src/impl/list_ports/list_ports_linux.cc \
    $(wildcard source/imgui/backends/*.cpp) \
    $(wildcard source/imgui/*.cpp) \
    $(wildcard source/stmdsp/*.cpp) \
    $(wildcard source/*.cpp)

OFILES := $(patsubst %.cc, %.o, $(patsubst %.cpp, %.o, $(CXXFILES)))
OUTPUT := stmdspgui

#CXXFLAGS := -std=c++20 -O2 \
#            -Isource -Isource/imgui -Isource/stmdsp -Isource/serial/include
CXXFLAGS := -std=c++20 -ggdb -O0 -g3 \
            -Isource -Isource/imgui -Isource/stmdsp -Isource/serial/include \
            -Wall -Wextra -pedantic

all: $(OUTPUT)

$(OUTPUT): $(OFILES)
	@echo "  LD    " $(OUTPUT)
	@g++ $(OFILES) -o $(OUTPUT) -lSDL2 -lGL -lpthread

clean:
	@echo "  CLEAN"
	@rm $(OFILES) $(OUTPUT)

%.o: %.cpp
	@echo "  CXX   " $<
	@g++ $(CXXFLAGS) -c $< -o $@

%.o: %.cc
	@echo "  CXX   " $<
	@g++ $(CXXFLAGS) -c $< -o $@


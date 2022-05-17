#linux: CXXFILES += source/serial/src/impl/unix.cc source/serial/src/impl/list_ports/list_ports_unix.cc
#linux: LDFLAGS = -lSDL2 -lGL -lpthread

#CROSS = x86_64-w64-mingw32-
#CXX = $(CROSS)g++
CXX = g++

CXXFILES := \
    source/serial/src/serial.cc \
    source/serial/src/impl/win.cc \
    source/serial/src/impl/list_ports/list_ports_win.cc \
    $(wildcard source/imgui/backends/*.cpp) \
    $(wildcard source/imgui/*.cpp) \
    $(wildcard source/stmdsp/*.cpp) \
    $(wildcard source/*.cpp)

OFILES := $(patsubst %.cc, %.o, $(patsubst %.cpp, %.o, $(CXXFILES)))
OUTPUT := stmdspgui.exe

CXXFLAGS := -std=c++20 -O2 \
            -Isource -Isource/imgui -Isource/stmdsp -Isource/serial/include \
            -Wall -Wextra -pedantic \
            -DSTMDSP_WIN32 -Wa,-mbig-obj
LDFLAGS = -mwindows -lSDL2 -lopengl32 -lsetupapi -lole32

all: $(OUTPUT)

$(OUTPUT): $(OFILES)
	@echo "  LD    " $(OUTPUT)
	@$(CXX) $(OFILES) -o $(OUTPUT) $(LDFLAGS)

clean:
	@echo "  CLEAN"
	@rm -f $(OFILES) $(OUTPUT)

%.o: %.cpp
	@echo "  CXX   " $<
	@$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.cc
	@echo "  CXX   " $<
	@$(CXX) $(CXXFLAGS) -c $< -o $@


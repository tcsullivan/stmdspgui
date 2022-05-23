CXX = g++

CXXFILES := \
    source/serial/src/serial.cc \
    $(wildcard source/imgui/backends/*.cpp) \
    $(wildcard source/imgui/*.cpp) \
    $(wildcard source/stmdsp/*.cpp) \
    $(wildcard source/*.cpp)

CXXFLAGS := -std=c++20 -O2 \
            -Isource -Isource/imgui -Isource/stmdsp -Isource/serial/include \
            -Wall -Wextra -pedantic #-DSTMDSP_DISABLE_FORMULAS

ifeq ($(OS),Windows_NT)
CXXFILES += source/serial/src/impl/win.cc \
            source/serial/src/impl/list_ports/list_ports_win.cc
CXXFLAGS += -DSTMDSP_WIN32 -Wa,-mbig-obj
LDFLAGS = -mwindows -lSDL2 -lopengl32 -lsetupapi -lole32
OUTPUT := stmdspgui.exe
else
CXXFILES += source/serial/src/impl/unix.cc \
            source/serial/src/impl/list_ports/list_ports_unix.cc
LDFLAGS = -lSDL2 -lGL -lpthread
OUTPUT := stmdspgui
endif

OFILES := $(patsubst %.cc, %.o, $(patsubst %.cpp, %.o, $(CXXFILES)))

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


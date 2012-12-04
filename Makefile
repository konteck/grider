CPP = g++
SRC = src/grider.cpp
OUT = grider
CPPFLAGS = # -O2 -Wall
LDFLAGS = -lzmq -lmongoclient -lboost_system-mt -lboost_thread-mt -lboost_filesystem-mt `GraphicsMagick++-config --cppflags --cxxflags --ldflags --libs`

all: build run

build: $(SRC)
	@echo Compiling $(basename $<)...
	$(CPP) $(CPPFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

run:
	./$(OUT)

install: build
	sudo cp $(OUT) /usr/bin

uninstall:
	sudo rm -rf /usr/bin/$(OUT)

clean:
	rm -rf *.dSYM

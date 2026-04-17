CXX ?= g++
ARCH ?= amd64

NANOMODBUS_VERSION ?= 1.23.0
SERIAL_VERSION ?= 2.5.0

COMMON_FLAGS = -g -Wall -Wextra -Iinclude -Ideps
CFLAGS += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) -std=c++23
LDFLAGS += -static $(shell pkg-config --libs --static ncurses readline)

NAME := mbsc
TARGET = build/$(NAME).$(ARCH)

C_SOURCES = $(shell find deps -type f -name "*.c")
CXX_SOURCES = $(wildcard src/*.cpp)

C_OBJS = $(C_SOURCES:.c=.o)
CXX_OBJS = $(CXX_SOURCES:.cpp=.o)

TEST_BIN = build/tests
TEST_SOURCES = $(wildcard tests/*_test.cpp)

.PHONY: all deps clean purge buildx test testx

all: $(TARGET)

$(TARGET): $(C_OBJS) $(CXX_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(C_OBJS) $(CXX_OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp include/$(NAME)/%.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

update_deps:
	@mkdir -p deps
	for ext in c h; do \
		wget -N -P deps/nanomodbus "https://raw.githubusercontent.com/debevv/nanoMODBUS/refs/tags/v$(NANOMODBUS_VERSION)/nanomodbus.$$ext"; \
		wget -N -P deps/serial "https://raw.githubusercontent.com/vsergeev/c-periphery/refs/tags/v$(SERIAL_VERSION)/src/serial.$$ext"; \
	done

clean:
	rm -f $(C_OBJS) $(CXX_OBJS) core.*

purge: clean
	rm -f build/*

test:
	g++ -g -std=c++23 -Wall -Wextra -Iinclude $(TEST_SOURCES) -lgtest -lgtest_main -pthread -o $(TEST_BIN)
	./$(TEST_BIN)

buildx: clean
	docker run --privileged --rm tonistiigi/binfmt --install all
	docker buildx build --platform linux/$(ARCH) -t $(NAME):$(ARCH) --load docker/
	docker run --rm --platform linux/$(ARCH) -v $(CURDIR):/app $(NAME):$(ARCH) make ARCH=$(ARCH)

testx:
	docker compose run --rm -v $(CURDIR):/app builder make test

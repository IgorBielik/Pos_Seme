CC=gcc
CFLAGS=
LDFLAGS_SERVER=-pthread

BUILD_DIR=build

SRV_SRCS=server.c game.c
CLI_SRCS=client.c

SRV_OBJS=$(addprefix $(BUILD_DIR)/, $(SRV_SRCS:.c=.o))
CLI_OBJS=$(addprefix $(BUILD_DIR)/, $(CLI_SRCS:.c=.o))

VALGRIND=valgrind
VALGRIND_FLAGS=--leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1

.PHONY: all server client clean valgrind-server valgrind-client

all: server client

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

server: $(SRV_OBJS)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/$@ $(SRV_OBJS) $(LDFLAGS_SERVER)

client: $(CLI_OBJS)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/$@ $(CLI_OBJS)

clean:
	rm -rf $(BUILD_DIR)

# Valgrind targets pre kontrolu memory leakov
valgrind-server: server
	@echo "Running valgrind on server..."
	$(VALGRIND) $(VALGRIND_FLAGS) $(BUILD_DIR)/server

valgrind-client: client
	@echo "Running valgrind on client..."
	$(VALGRIND) $(VALGRIND_FLAGS) $(BUILD_DIR)/client

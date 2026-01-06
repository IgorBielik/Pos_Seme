CC=gcc
CFLAGS=
LDFLAGS_SERVER=-pthread

BUILD_DIR=build

SRV_SRCS=server.c game.c
CLI_SRCS=client.c

SRV_OBJS=$(addprefix $(BUILD_DIR)/, $(SRV_SRCS:.c=.o))
CLI_OBJS=$(addprefix $(BUILD_DIR)/, $(CLI_SRCS:.c=.o))

.PHONY: all server client clean

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

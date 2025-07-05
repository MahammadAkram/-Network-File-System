# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -g

# Target names
TARGETS = naming_server storage_server client

# Object files
OBJ_NAMING = naming_server.o helper.o tries.o
OBJ_STORAGE = storage_server.o helper.o tries.o lock.o
OBJ_CLIENT = client.o helper.o

# IP Address for server
IPADD = 127.0.0.1

# Default target
all: $(TARGETS)

# Naming Server executable
naming_server: $(OBJ_NAMING)
	$(CC) $(CFLAGS) -o naming_server $(OBJ_NAMING)

# Storage Server executable
storage_server: $(OBJ_STORAGE)
	$(CC) $(CFLAGS) -o storage_server $(OBJ_STORAGE)

# Client executable
client: $(OBJ_CLIENT)
	$(CC) $(CFLAGS) -o client $(OBJ_CLIENT)

# Compile naming_server.c
naming_server.o: naming_server.c helper.h tries.h ErrorCodes.h
	$(CC) $(CFLAGS) -c naming_server.c

# Compile storage_server.c
storage_server.o: storage_server.c helper.h tries.h ErrorCodes.h lock.h
	$(CC) $(CFLAGS) -c storage_server.c

# Compile client.c
client.o: client.c helper.h ErrorCodes.h
	$(CC) $(CFLAGS) -c client.c

# Compile helper.c
helper.o: helper.c helper.h ErrorCodes.h
	$(CC) $(CFLAGS) -c helper.c

# Compile tries.c
tries.o: tries.c tries.h ErrorCodes.h
	$(CC) $(CFLAGS) -c tries.c

# Compile tries.c
lock.o: lock.c lock.h ErrorCodes.h
	$(CC) $(CFLAGS) -c lock.c

# Clean up
clean:
	rm -f $(TARGETS) *.o

# Run Naming Server
ns: naming_server
	rm -rf ./backupfolderforss
	./naming_server $(PORT)

# Run Storage Server
ss: storage_server
	./storage_server $(IPADD) ./mouli

# Run Client
cl: client
	./client $(IPADD)

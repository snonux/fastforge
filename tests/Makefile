CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -g

TZ ?= UTC
export TZ

ROOT := ..
UNITY_DIR := vendor/unity/src
SRC_DIR := $(ROOT)/src/c

TARGET := test_fastforge

SRCS := \
	$(UNITY_DIR)/unity.c \
	$(SRC_DIR)/fastforge_logic.c \
	$(SRC_DIR)/fastforge_csv.c \
	$(wildcard test_*.c)

INCLUDES := -I. -I$(UNITY_DIR) -I$(SRC_DIR)

.PHONY: all test clean

all: test

test: $(TARGET)
	TZ=$(TZ) ./$(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRCS) -o $@

clean:
	rm -f $(TARGET)

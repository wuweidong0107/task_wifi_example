
CROSS_COMPILE ?=
AS		= $(CROSS_COMPILE)as
LD		= $(CROSS_COMPILE)ld
CC		= $(CROSS_COMPILE)gcc
CPP		= $(CC) -E
AR		= $(CROSS_COMPILE)ar
NM		= $(CROSS_COMPILE)nm

STRIP		= $(CROSS_COMPILE)strip
OBJCOPY		= $(CROSS_COMPILE)objcopy
OBJDUMP		= $(CROSS_COMPILE)objdump

export AS LD CC CPP AR NM
export STRIP OBJCOPY OBJDUMP

CFLAGS := -Wall -O2 -g
CFLAGS += -I$(shell pwd)/ -I$(shell pwd)/wifi 

LDFLAGS := -lpthread

export CFLAGS LDFLAGS

TOPDIR := $(shell pwd)
export TOPDIR

TARGET := task_wifi

obj-y += wifi/
obj-y += wifi.o
obj-y += thpool.o
obj-y += stdstring.o
obj-y += task_wifi.o

all : 
	make -C ./ -f $(TOPDIR)/Makefile.build
	$(CC) -o $(TARGET) built-in.o $(LDFLAGS)

clean:
	@echo "cleaning..."
	@rm -f $(shell find -type f -name "*.o")
	@rm -f $(shell find -type f -name "*.d")
	@rm -f $(TARGET)
	
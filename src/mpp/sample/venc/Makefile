
include ../Makefile.param

CFLAGS += -I$(PWD)/../svp/common

SMP_SRCS := $(wildcard *.c)
TARGET := sample_venc

TARGET_PATH := $(PWD)

# compile linux or LITEOS
include $(PWD)/../$(ARM_ARCH)_$(OSTYPE).mak

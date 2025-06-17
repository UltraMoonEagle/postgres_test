# src/backend/access/blockchain/Makefile

subdir = src/backend/access/blockchain
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global

OBJS = \
		blockchainam.o \
		blockchain_utility.o \
		blockchain_hash.o \
		 $(top_builddir)/src/common/sha2.o

override SHLIB_LINK += -luuid

include $(top_srcdir)/src/backend/common.mk


OS=$(shell uname -s)
ifeq (,$(findstring Windows,$(OS)))
include Makefile.linux
else
include Makefile.win32
endif

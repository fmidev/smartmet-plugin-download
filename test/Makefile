PROG = $(patsubst %.cpp,%,$(wildcard *Test.cpp))

FLAGS = -std=c++11 -Wall -W -Wno-unused-parameter

EXTRAFLAGS = -Werror -pedantic -Wpointer-arith -Wcast-qual \
	-Wcast-align -Wwrite-strings -Wconversion -Winline \
	-Wctor-dtor-privacy -Wnon-virtual-dtor -Wno-pmf-conversions \
	-Wsign-promo -Wchar-subscripts -Wold-style-cast \
	-Wshadow -Woverloaded-virtual

DIFFICULTFLAGS = -Weffc++ -Wunreachable-code -Wredundant-decls

ifeq ($(TSAN), yes)
  FLAGS += -fsanitize=thread
endif
ifeq ($(ASAN), yes)
  FLAGS += -fsanitize=address -fsanitize=pointer-compare -fsanitize=pointer-subtract -fsanitize=undefined -fsanitize-address-use-after-scope
endif

CC = g++
CFLAGS = -DUNIX -O0 -g $(FLAGS)
INCLUDES = -I ../download -I /usr/include/smartmet -I /usr/include/mysql

LIBS =	-lsmartmet-spine \
	-lsmartmet-newbase \
	-lsmartmet-macgyver \
	-lboost_date_time \
	-lboost_thread \
	-lboost_filesystem \
	-lboost_iostreams \
	-lboost_system \
	-lconfig++ \
	-lbz2 -lz \
	-leccodes \
	-lgdal \
	-lpthread \
	-L/usr/lib64/mysql -lmysqlclient_r

OBS =	../obj/Config.o \
		../obj/DataStreamer.o \
		../obj/Datum.o \
		../obj/GribStreamer.o \
		../obj/GribTools.o \
		../obj/NetCdfStreamer.o \
		../obj/Plugin.o \
		../obj/QueryDataStreamer.o \
		../obj/Query.o

all: $(PROG)

clean:
	rm -f $(PROG) *~

test: $(PROG)
	@echo Running tests:
	@mkdir -p tmp failures
	@rm -f tmp/* failures/*
	@for prog in $(PROG); do \
	  ./$$prog; \
	done

$(PROG) : % : %.cpp Makefile ../download.so
	$(CC) $(CFLAGS) -o $@ $@.cpp $(INCLUDES) $(LIBS)

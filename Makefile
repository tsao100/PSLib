# Makefile for building Fortran + C Motif GUI app

FC      = gfortran
CC      = gcc
CFLAGS  = -g -fPIC -I/usr/include/X11
LDFLAGS = -lXm -lXt -lX11 -lXpm -lGL -lGLU -lm
TARGET  = PSLIB_TEST
OBJS    = PSLib.o
SRCF    = main.f
SRCC    = PSLib.c

all: $(TARGET)

$(OBJS): $(SRCC)
	$(CC) $(CFLAGS) -c $(SRCC) -o $(OBJS)

$(TARGET): $(SRCF) $(OBJS)
	$(FC) -g $(SRCF) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET) $(OBJS)

# Makefile for building Fortran + C Motif GUI app

FC      = gfortran
CC      = gcc
CFLAGS  = -g -fPIC -I/usr/include/X11
LDFLAGS = -lXm -lXt -lX11 -lXpm
TARGET  = PSLIB_TEST
OBJS    = PSLib.o tinyspline.o parson.o
SRCF    = main.f
SRCC    = PSLib.c

all: $(TARGET)

# Rule for PSLib.o
PSLib.o: PSLib.c tinyspline.h parson.h
	$(CC) $(CFLAGS) -c PSLib.c -o PSLib.o

# Rule for parson.o
parson.o: parson.c parson.h
	$(CC) $(CFLAGS) -c parson.c -o parson.o

# Rule for tinyspline.o
tinyspline.o: tinyspline.c tinyspline.h
	$(CC) $(CFLAGS) -c tinyspline.c -o tinyspline.o

# Link everything with Fortran
$(TARGET): $(SRCF) $(OBJS)
	$(FC) -g $(SRCF) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET) $(OBJS)

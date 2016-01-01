INCLUDENAME = libfpga.h
LIBNAME = libfpga.so
SOURCE = libfpga.c 
OBJECT = $(SOURCE:.c=.o)

all: $(LIBNAME)

$(LIBNAME) : $(OBJECT)
	gcc -shared -o $(LIBNAME) $(OBJECT)
$(OBJECT) : $(SOURCE) 
	gcc -c -fPIC $(SOURCE) 

install:
	cp $(LIBNAME) /usr/lib/
	chmod 0775 /usr/lib/$(LIBNAME)
	ldconfig
	cp $(INCLUDENAME) /usr/include

clean:
	rm -f $(OBJECT)
	rm -f *.so

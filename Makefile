APXS = /usr/local/apache2/bin/apxs

.SUFFIXES: .c .o .so

MODULES = mod_limits.so

all: $(MODULES)

.c.so: $*.c
	$(APXS) -c $(DEF) $(INC) $(LIB) $*.c

install: all
	@for i in $(MODULES); do \
		$(APXS) -ia $$i; \
	done

clean:
	rm -f *.o *.so

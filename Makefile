APXS = /usr/local/apache2/bin/apxs

.SUFFIXES: .c .o .so

MODULES = mod_limits.so

all: $(MODULES)

.c.so: $*.c
	$(APXS) -c $(DEF) $(INC) $(LIB) $*.c

install: all
	@for i in $(MODULES); do \
		$(APXS) -cia $${i/.so/.c}; \
	done

clean:
	rm -f *.o *.so *.lo *.la *.slo

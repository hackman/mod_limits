APXS2 = /usr/local/apache2/bin/apxs
APXS = /usr/local/apache/bin/apxs
MODULE = mod_limits
MODULES = mod_limits.so
.SUFFIXES: .c .o .so


ap13: mod_limits.c
	$(APXS) -c $(MODULE).c

all: $(MODULES)

.c.so: $*.c
	$(APXS2) -c $(DEF) $(INC) $(LIB) $*.c

install: all
	@for i in $(MODULES); do \
		$(APXS) -cia $${i/.so/.c}; \
	done

clean:
	rm -f *.o *.so *.lo *.la *.slo

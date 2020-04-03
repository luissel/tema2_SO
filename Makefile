CFLAGS = -Wextra -Wall -g

build:
	gcc $(CFLAGS) -fPIC -c so_stdio.c
	gcc $(CFLAGS) -shared so_stdio.o -o libso_stdio.so
	
clean:
	rm -f so_stdio.o libso_stdio.so

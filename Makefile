

launcher: launcher.c
	gcc -g -Os -Wall -Wno-unused-function -o launcher launcher.c
	strip launcher

clean:
	rm -f launcher launcher.o

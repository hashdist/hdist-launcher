

hdist-launcher: hdist-launcher.c
	gcc -g -Os -Wall -Wno-unused-function -o hdist-launcher hdist-launcher.c
	strip hdist-launcher

clean:
	rm -f hdist-launcher hdist-launcher.o

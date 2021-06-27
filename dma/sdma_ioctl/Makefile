CC=arm-linux-gnueabihf-gcc
all : sdma

app : sdma.c
	$(CC) -o $@ $^
clean :
	rm sdma
deploy : sdma
	scp $^ root@raspberrypi.local:

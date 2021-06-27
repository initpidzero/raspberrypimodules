#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define SDMA_BUFFER_SIZE (4 * 1024)

int main(void)
{
	char *virtaddr;
	char phrase[SDMA_BUFFER_SIZE];

	int mydev = open("/dev/sdma_test", O_RDWR);
	if (mydev < 0) {
		fprintf(stderr, "%s\n", strerror(errno));
		return -1;
	}

	printf("enter phrase :\n");
	scanf("%[^\n]%*c", phrase);
	virtaddr = (char *)mmap(0, SDMA_BUFFER_SIZE, PROT_READ | PROT_WRITE,
		       MAP_SHARED, mydev, 0);
	strcpy(virtaddr, phrase);
	printf("copied = %s\n", virtaddr);
	ioctl(mydev, 0, 0);
	close(mydev);
	munmap(virtaddr, SDMA_BUFFER_SIZE);

	return 0;

}

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

void run_forever() {
	time_t seconds;
	while(1) {
		seconds = time(NULL);
		if (seconds % 20 == 0) {
			sleep(1);
		}
	}

}

int main(int argc, char** argv) {
	pid_t pid = fork();

	if (pid == -1) {
		printf("fork failed :P");
		exit(1);
	} else if (pid == 0) {
		chdir("/");
		run_forever();
	} else {
		printf("forked!");
		unlink("squared");
		exit(0);
	}

	return 0;
}

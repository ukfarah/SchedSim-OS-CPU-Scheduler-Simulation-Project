#include "headers.h"

int *shmaddr;

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <id> <runtime>\n", argv[0]);
        return 1;
    }

    int id = atoi(argv[1]);
    int runtime = atoi(argv[2]);
    (void)id;

    initClk();

    int elapsed = 0;
    int last = getClk();

    while (elapsed < runtime)
    {
        int now = getClk();
        if (now == last + 1)
        {
            elapsed += 1;
        }
        if (now != last)
        {
            last = now;
        }
        else
        {
            usleep(2000);
        }
    }

    kill(getppid(), SIGUSR1);

    destroyClk(false);
    return 0;
}
/* Минимальные заглушки сисколлов newlib для bare-metal JN5169 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

#undef errno
extern int errno;

/* Статическая куча: malloc у RCP почти не используется (vsnprintf и пр.) */
#define HEAP_SIZE 2048
static uint8_t sHeap[HEAP_SIZE] __attribute__((aligned(4)));
static size_t  sHeapOffset;

void *_sbrk(ptrdiff_t aIncrement)
{
    void *prev;

    if (sHeapOffset + (size_t)aIncrement > HEAP_SIZE)
    {
        errno = ENOMEM;
        return (void *)-1;
    }

    prev = &sHeap[sHeapOffset];
    sHeapOffset += (size_t)aIncrement;
    return prev;
}

int _close(int aFd)
{
    (void)aFd;
    return -1;
}

int _fstat(int aFd, struct stat *aSt)
{
    (void)aFd;
    aSt->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int aFd)
{
    (void)aFd;
    return 1;
}

int _lseek(int aFd, int aOffset, int aWhence)
{
    (void)aFd;
    (void)aOffset;
    (void)aWhence;
    return 0;
}

int _read(int aFd, char *aBuf, int aLen)
{
    (void)aFd;
    (void)aBuf;
    (void)aLen;
    return 0;
}

int _write(int aFd, const char *aBuf, int aLen)
{
    (void)aFd;
    (void)aBuf;
    return aLen;
}

int _getpid(void)
{
    return 1;
}

int _kill(int aPid, int aSig)
{
    (void)aPid;
    (void)aSig;
    errno = EINVAL;
    return -1;
}

void _exit(int aStatus)
{
    (void)aStatus;
    for (;;)
    {
    }
}

/* Точки входа JN5169: ROM-загрузчик сам раскладывает .data/.bss по заголовку
 * .bir и прыгает в AppColdStart. Остаётся позвать main(). */
#include <stdint.h>

extern int  main(void);
extern void vAHI_WatchdogStop(void);

/* Конструкторы C++ (у ядра OT глобальных почти нет, но на всякий случай) */
typedef void (*initFn)(void);
extern initFn __init_array_start[] __attribute__((weak));
extern initFn __init_array_end[] __attribute__((weak));

static void runInitArray(void)
{
    initFn *fn;

    if (__init_array_start == 0)
    {
        return;
    }

    for (fn = __init_array_start; fn < __init_array_end; fn++)
    {
        (*fn)();
    }
}

void AppColdStart(void)
{
    vAHI_WatchdogStop(); /* вачдог активен с ресета */
    runInitArray();
    main();

    for (;;)
    {
    }
}

void AppWarmStart(void)
{
    AppColdStart();
}

#include <zephyr/kernel.h>
#include <zephyr/fatal.h>
#include <cmsis_core.h>

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
    ARG_UNUSED(reason);
    ARG_UNUSED(esf);
    NVIC_SystemReset();
    CODE_UNREACHABLE;
}

void bt_ctlr_assert_handle(char *file, uint32_t line)
{
    ARG_UNUSED(file);
    ARG_UNUSED(line);
    NVIC_SystemReset();
    CODE_UNREACHABLE;
}

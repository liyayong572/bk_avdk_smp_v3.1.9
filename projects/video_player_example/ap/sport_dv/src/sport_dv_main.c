#include <os/os.h>
#include "bk_private/bk_init.h"
#include <media_service.h>

#include <os/mem.h>
#include "bk_private/bk_init.h"
#include <components/system.h>
#include <components/shell_task.h>
#include "cli.h"

#include "sport_dv.h"

int main(void)
{
    bk_init();
    media_service_init();
    (void)sport_dv_start();
    return 0;
}

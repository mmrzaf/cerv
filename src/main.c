#include "process/config.h"
#include "process/diag.h"
#include "process/supervisor.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    cerv_diag_prepare();
    struct cerv_config config;
    struct cerv_config_env env;
    char error[160] = {0};
    cerv_config_env_read_process(&env);
    enum cerv_config_result result = cerv_config_parse_with_env(argc, argv, &env, &config, error, sizeof(error));
    if (result == CERV_CONFIG_HELP) {
        (void)fputs(cerv_config_help_text(), stdout);
        return 0;
    }
    if (result == CERV_CONFIG_VERSION) {
        (void)fputs(cerv_version_text(), stdout);
        return 0;
    }
    if (result != CERV_CONFIG_OK) {
        if (error[0] != '\0') cerv_diag_message("error", "config", error);
        else cerv_diag_message("error", "config", "invalid arguments");
        return 2;
    }
    return cerv_supervisor_run(&config);
}

#include "app.h"
#include "platform.h"
#include "process.h"

int app_run(const app_config_t *config, int argc, char *argv[]) {
    int res = platform_app_run(config, argc, argv);
    process_cleanup_all();
    return res;
}
void app_emit(const char *event, const char *json_data) {
    platform_app_emit(event, json_data);
}
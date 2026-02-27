#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PORT 8448

// ---------------- Connection Info ----------------
struct connection_info {
    char *data;
    size_t size;
};

// ---------------- JSON Response ----------------
static enum MHD_Result respond_json(struct MHD_Connection *connection,
                                    unsigned int status,
                                    const char *body)
{
    struct MHD_Response *resp =
        MHD_create_response_from_buffer(strlen(body),
                                        (void *)body,
                                        MHD_RESPMEM_MUST_COPY);

    MHD_add_response_header(resp, "Content-Type", "application/json");

    enum MHD_Result ret =
        MHD_queue_response(connection, status, resp);

    MHD_destroy_response(resp);
    return ret;
}

// ---------------- ROS2 Publisher ----------------
void mp2_set_velocity(float linear, float angular)
{
    char cmd[512];

    snprintf(cmd, sizeof(cmd),
        "ros2 topic pub -r 1 /cmd_vel geometry_msgs/msg/Twist "
        "\"{linear: {x: %.2f, y: 0.0, z: 0.0}, "
        "angular: {x: 0.0, y: 0.0, z: %.2f}}\"",
        linear, angular);

    printf("Executing: %s\n", cmd);
    system(cmd);
}

// ---------------- Direction Mapping ----------------
void process_move_command(const char *dir)
{
    float linear = 0.0;
    float angular = 0.0;

    if (strcmp(dir, "forward") == 0)
        linear = 0.5;
    else if (strcmp(dir, "backward") == 0)
        linear = -0.5;
    else if (strcmp(dir, "left") == 0)
        angular = 1.0;
    else if (strcmp(dir, "right") == 0)
        angular = -1.0;
    else {
        printf("Unknown move_dir: %s\n", dir);
        return;
    }

    mp2_set_velocity(linear, angular);
}

// ---------------- POST Handler ----------------
static enum MHD_Result handle_post(void *cls,
                                   struct MHD_Connection *connection,
                                   const char *url,
                                   const char *method,
                                   const char *version,
                                   const char *upload_data,
                                   size_t *upload_data_size,
                                   void **con_cls)
{
    if (strcmp(method, "POST") != 0)
        return MHD_NO;

    if (*con_cls == NULL) {
        struct connection_info *ci =
            calloc(1, sizeof(struct connection_info));
        *con_cls = ci;
        return MHD_YES;
    }

    struct connection_info *ci =
        (struct connection_info *)*con_cls;

    // Collect POST data
    if (*upload_data_size != 0) {
        ci->data = realloc(ci->data,
                           ci->size + *upload_data_size + 1);

        memcpy(ci->data + ci->size,
               upload_data,
               *upload_data_size);

        ci->size += *upload_data_size;
        ci->data[ci->size] = '\0';

        *upload_data_size = 0;
        return MHD_YES;
    }

    // -------- Process JSON --------
    printf("Received JSON:\n%s\n", ci->data);

    char move_dir[64] = {0};
    char *pos = strstr(ci->data, "\"move_dir\"");

    if (pos) {
        sscanf(pos,
               "\"move_dir\"%*[^:]:\"%63[^\"]\"",
               move_dir);

        printf("Parsed move_dir: %s\n", move_dir);
        process_move_command(move_dir);
    }
    else {
        printf("move_dir not found in JSON\n");
    }

    free(ci->data);
    free(ci);
    *con_cls = NULL;

    return respond_json(connection,
                        MHD_HTTP_OK,
                        "{\"status\":\"success\"}");
}

// ---------------- Load TLS File ----------------
static char *load_file(const char *filename)
{
    FILE *f = fopen(filename, "r");
    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = '\0';

    fclose(f);
    return buf;
}

// ---------------- MAIN ----------------
int main()
{
    char *cert = load_file("certs/server.crt");
    char *key  = load_file("certs/server.key");

    if (!cert || !key) {
        printf("Failed to load TLS certificate/key\n");
        return 1;
    }

    struct MHD_Daemon *daemon =
        MHD_start_daemon(
            MHD_USE_THREAD_PER_CONNECTION |
            MHD_USE_TLS,
            PORT,
            NULL, NULL,
            &handle_post, NULL,
            MHD_OPTION_HTTPS_MEM_CERT, cert,
            MHD_OPTION_HTTPS_MEM_KEY, key,
            MHD_OPTION_END);

    if (!daemon) {
        printf("Failed to start HTTPS server\n");
        return 1;
    }

    printf("Mini Pupper HTTPS control running at:\n");
    printf("https://localhost:%d\n", PORT);

    while (1)
        sleep(1);

    MHD_stop_daemon(daemon);
    free(cert);
    free(key);
    return 0;
}

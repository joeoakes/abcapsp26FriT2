#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Port for HTTPS server
#define PORT 8448
#define POSTBUFFERSIZE 65536

// Connection info for incoming POST data
struct connection_info {
    char *data;
    size_t size;
};

// ---- SDK Function Stub ----
// Replace this with your actual Mini Pupper 2 SDK include
// and function call
void mp2_set_velocity(float linear, float angular) {
    // This function should move the Mini Pupper using the SDK
    printf("[Mini Pupper 2] Moving - Linear: %.2f, Angular: %.2f\n", linear, angular);
}

// Helper to respond with JSON
static int respond_json(struct MHD_Connection *connection, unsigned int status, const char *body) {
    struct MHD_Response *resp = MHD_create_response_from_buffer(strlen(body), (void *)body, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    int ret = MHD_queue_response(connection, status, resp);
    MHD_destroy_response(resp);
    return ret;
}

// Function to handle movement commands
void move_pupper(float linear, float angular) {
    mp2_set_velocity(linear, angular);
}

// POST request handler
static int handle_post(void *cls, struct MHD_Connection *connection, const char *url,
                       const char *method, const char *version, const char *upload_data,
                       size_t *upload_data_size, void **con_cls) {

    if (strcmp(method, "POST") != 0) return MHD_NO;

    if (*con_cls == NULL) {
        struct connection_info *ci = calloc(1, sizeof(struct connection_info));
        *con_cls = ci;
        return MHD_YES;
    }

    struct connection_info *ci = (struct connection_info *)*con_cls;

    if (*upload_data_size != 0) {
        ci->data = realloc(ci->data, ci->size + *upload_data_size + 1);
        memcpy(ci->data + ci->size, upload_data, *upload_data_size);
        ci->size += *upload_data_size;
        ci->data[ci->size] = '\0';
        *upload_data_size = 0;
        return MHD_YES;
    }

    printf("Received: %s\n", ci->data);

    // Parse linear/angular from JSON (simple sscanf)
    float linear = 0.0, angular = 0.0;
    sscanf(ci->data, "{\"linear\":%f,\"angular\":%f}", &linear, &angular);

    move_pupper(linear, angular);

    return respond_json(connection, MHD_HTTP_OK, "{\"status\":\"success\"}");
}

// Load certificate file into memory
static char *load_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

int main() {
    char *cert = load_file("certs/server.crt");
    char *key  = load_file("certs/server.key");

    if (!cert || !key) {
        printf("Failed to load certificate or key\n");
        return 1;
    }

    struct MHD_Daemon *daemon = MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION | MHD_USE_TLS, PORT,
        NULL, NULL, &handle_post, NULL,
        MHD_OPTION_HTTPS_MEM_CERT, cert,
        MHD_OPTION_HTTPS_MEM_KEY, key,
        MHD_OPTION_END);

    if (!daemon) {
        printf("Failed to start HTTPS server\n");
        return 1;
    }

    printf("Mini Pupper 2 HTTPS control running on https://localhost:%d\n", PORT);

    while (1) sleep(1); // Keep server alive

    MHD_stop_daemon(daemon);
    free(cert);
    free(key);
    return 0;
}

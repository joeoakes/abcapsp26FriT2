#include <microhttpd.h>
#include <hiredis/hiredis.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_PORT 6379
#define POSTBUFFERSIZE 65536

struct connection_info {
    char *data;
    size_t size;
};

static enum MHD_Result respond_json(struct MHD_Connection *connection, unsigned int status, const char *body) {
    struct MHD_Response *resp = MHD_create_response_from_buffer(strlen(body), (void *)body, MHD_RESPMEM_PERSISTENT);
    if (!resp) return MHD_NO;
    MHD_add_response_header(resp, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(connection, status, resp);
    MHD_destroy_response(resp);
    return ret;
}

// Update handle_post to return enum MHD_Result
static enum MHD_Result handle_post(void *cls, struct MHD_Connection *connection, const char *url, const char *method,
                       const char *version, const char *upload_data, size_t *upload_data_size, void **con_cls) {
    if (strcmp(url, "/mission") != 0) {
        return respond_json(connection, 404, "{\"error\":\"Only /mission supported\"}");
    }

    if (*con_cls == NULL) {
        struct connection_info *ci = calloc(1, sizeof(*ci));
        ci->data = malloc(1);
        ci->size = 0;
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

    // Connect to Redis
    redisContext *c = redisConnect("127.0.0.1", 6379);
    if (c == NULL || c->err) {
        return respond_json(connection, 500, "{\"error\":\"Redis connection failed\"}");
    }

    // Save payload to Redis Hash
    // Note: In a real app, you'd parse the JSON to get a real mission_id
    redisCommand(c, "HSET mission:T2_FRI:summary payload %s status active", ci->data);
    
    redisFree(c);
    free(ci->data);
    free(ci);
    *con_cls = NULL;

    return respond_json(connection, 200, "{\"status\":\"ok\",\"stored\":\"redis\"}");
}

int main() {
    int port = getenv("LISTEN_PORT") ? atoi(getenv("LISTEN_PORT")) : DEFAULT_PORT;
    struct MHD_Daemon *d = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, port, NULL, NULL, &handle_post, NULL, MHD_OPTION_END);
    
    if (!d) return 1;
    printf("Redis-only Mission Server listening on port %d/mission\n", port);
    getchar();
    MHD_stop_daemon(d);
    return 0;
}

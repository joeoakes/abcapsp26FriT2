#include <microhttpd.h>
#include <hiredis/hiredis.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_PORT 8448

// 1. Data Structures
struct connection_info {
    char *data;
    size_t size;
};

// 2. Helper to load SSL Certificates
static char *load_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

// 3. Response Helper
static enum MHD_Result respond_json(struct MHD_Connection *connection, unsigned int status, const char *body) {
    struct MHD_Response *resp = MHD_create_response_from_buffer(strlen(body), (void *)body, MHD_RESPMEM_PERSISTENT);
    if (!resp) return MHD_NO;
    MHD_add_response_header(resp, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(connection, status, resp);
    MHD_destroy_response(resp);
    return ret;
}

// 4. POST Handler (Redis Logic)
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
    redisCommand(c, "HSET mission:T2_FRI:summary payload %s status active", ci->data);
    
    redisFree(c);
    free(ci->data);
    free(ci);
    *con_cls = NULL;

    return respond_json(connection, 200, "{\"status\":\"ok\",\"stored\":\"redis\"}");
}

// 5. Main (The Start-up Logic)
int main() {
    // Load SSL certs
    char *cert_pem = load_file("certs/server.crt"); 
    char *key_pem  = load_file("certs/server.key");

    if (!cert_pem || !key_pem) {
        fprintf(stderr, "SSL Error: Could not load certs/server.crt or certs/server.key\n");
        return 1;
    }

    int port = getenv("LISTEN_PORT") ? atoi(getenv("LISTEN_PORT")) : DEFAULT_PORT;

    struct MHD_Daemon *d = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_TLS, 
        (uint16_t)port,
        NULL, NULL,
        &handle_post, NULL,
        MHD_OPTION_HTTPS_MEM_CERT, cert_pem,
        MHD_OPTION_HTTPS_MEM_KEY,  key_pem,
        MHD_OPTION_END
    );
    
    if (!d) {
        fprintf(stderr, "Failed to start HTTPS daemon\n");
        return 1;
    }

    printf("Redis HTTPS Mission Server listening on port %d/mission\n", port);
    printf("Press Enter to stop...\n");
    getchar();

    MHD_stop_daemon(d);
    free(cert_pem);
    free(key_pem);
    return 0;
}

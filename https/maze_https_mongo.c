#include <microhttpd.h>
#include <mongoc/mongoc.h>
#include <bson/bson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_PORT 8443
#define POSTBUFFERSIZE  4096
#define MAXNAMESIZE     64
#define MAXANSWERSIZE   512

static const char *cert_file = "certs/server.crt";
static const char *key_file  = "certs/server.key";

struct connection_info {
    char *data;
    size_t size;
};

static void get_utc_iso8601(char *buf, size_t len) {
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

static int handle_post(void *cls,
                       struct MHD_Connection *connection,
                       const char *url,
                       const char *method,
                       const char *version,
                       const char *upload_data,
                       size_t *upload_data_size,
                       void **con_cls)
{
    (void)version;
    (void)cls;

    if (strcmp(method, "POST") != 0 || strcmp(url, "/move") != 0)
        return MHD_NO;

    if (*con_cls == NULL) {
        struct connection_info *ci = calloc(1, sizeof(*ci));
        *con_cls = ci;
        return MHD_YES;
    }

    struct connection_info *ci = *con_cls;

    if (*upload_data_size != 0) {
        ci->data = realloc(ci->data, ci->size + *upload_data_size + 1);
        memcpy(ci->data + ci->size, upload_data, *upload_data_size);
        ci->size += *upload_data_size;
        ci->data[ci->size] = '\0';
        *upload_data_size = 0;
        return MHD_YES;
    }

    /* MongoDB insert */
    bson_error_t error;
    bson_t *doc = bson_new_from_json((uint8_t *)ci->data, -1, &error);
    if (!doc) {
        fprintf(stderr, "JSON error: %s\n", error.message);
        return MHD_NO;
    }

    char ts[64];
    get_utc_iso8601(ts, sizeof(ts));
    BSON_APPEND_UTF8(doc, "received_at", ts);

    mongoc_client_t *client = mongoc_client_new(getenv("MONGO_URI"));
    mongoc_collection_t *col =
        mongoc_client_get_collection(client,
                                      getenv("MONGO_DB"),
                                      getenv("MONGO_COL"));

    mongoc_collection_insert_one(col, doc, NULL, NULL, &error);

    mongoc_collection_destroy(col);
    mongoc_client_destroy(client);
    bson_destroy(doc);

    const char *response = "{\"status\":\"ok\"}";
    struct MHD_Response *resp =
        MHD_create_response_from_buffer(strlen(response),
                                         (void *)response,
                                         MHD_RESPMEM_PERSISTENT);

    int ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);

    free(ci->data);
    free(ci);
    *con_cls = NULL;

    return ret;
}
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

// Helper to load file content into a string
static char *load_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("fopen");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    if (buf) {
        fread(buf, 1, len, f);
        buf[len] = '\0';
    }
    fclose(f);
    return buf;
}

int main(void) {
    // 1. Get Environment Variables
    const char *cert_path = getenv("CERT_FILE") ? getenv("CERT_FILE") : "certs/server.crt";
    const char *key_path  = getenv("KEY_FILE")  ? getenv("KEY_FILE")  : "certs/server.key";
    int port = getenv("LISTEN_PORT") ? atoi(getenv("LISTEN_PORT")) : DEFAULT_PORT;

    // 2. Load Certificates into Memory (Required for MHD_OPTION_HTTPS_MEM_*)
    char *cert_pem = load_file(cert_path);
    char *key_pem  = load_file(key_path);

    if (!cert_pem || !key_pem) {
        fprintf(stderr, "Error: Could not load certificates from %s or %s\n", cert_path, key_path);
        return 1;
    }

    mongoc_init();

    // 3. Start Daemon with actual PEM content
    struct MHD_Daemon *daemon = MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION | MHD_USE_TLS,
        port,
        NULL, NULL,
        &handle_post, NULL,
        MHD_OPTION_HTTPS_MEM_CERT, cert_pem,
        MHD_OPTION_HTTPS_MEM_KEY,  key_pem,
        MHD_OPTION_END);

    if (!daemon) {
        fprintf(stderr, "Failed to start HTTPS server. Check if port %d is open.\n", port);
        free(cert_pem); free(key_pem);
        return 1;
    }

    printf("HTTPS server listening on https://localhost:%d/move\n", port);
    getchar();

    MHD_stop_daemon(daemon);
    mongoc_cleanup();
    free(cert_pem);
    free(key_pem);
    return 0;
}

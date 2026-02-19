#include <microhttpd.h>
#include <mongoc/mongoc.h>
#include <bson/bson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_PORT 8448
#define POSTBUFFERSIZE  65536

struct connection_info {
    char *data;
    size_t size;
};

static void get_utc_iso8601(char *buf, size_t len) {
    time_t now = time(NULL);
    struct tm tmv;
    gmtime_r(&now, &tmv);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

static int respond_json(struct MHD_Connection *connection, unsigned int status, const char *body) {
    struct MHD_Response *resp =
        MHD_create_response_from_buffer(strlen(body), (void *)body, MHD_RESPMEM_PERSISTENT);
    if (!resp) return MHD_NO;
    MHD_add_response_header(resp, "Content-Type", "application/json");
    int ret = MHD_queue_response(connection, status, resp);
    MHD_destroy_response(resp);
    return ret;
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

    // Only support POST /move
    if (strcmp(url, "/move") != 0) {
        return respond_json(connection, MHD_HTTP_NOT_FOUND,
                            "{\"status\":\"error\",\"message\":\"not found\"}");
    }
    if (strcmp(method, "POST") != 0) {
        return respond_json(connection, MHD_HTTP_METHOD_NOT_ALLOWED,
                            "{\"status\":\"error\",\"message\":\"method not allowed\"}");
    }

    if (*con_cls == NULL) {
        struct connection_info *ci = calloc(1, sizeof(*ci));
        if (!ci) return MHD_NO;
        ci->data = calloc(1, 1);
        ci->size = 0;
        *con_cls = ci;
        return MHD_YES;
    }

    struct connection_info *ci = (struct connection_info *)(*con_cls);

    if (*upload_data_size != 0) {
        if (ci->size + *upload_data_size > POSTBUFFERSIZE) {
            free(ci->data);
            free(ci);
            *con_cls = NULL;
            return respond_json(connection, MHD_HTTP_REQUEST_ENTITY_TOO_LARGE,
                                "{\"status\":\"error\",\"message\":\"payload too large\"}");
        }

        char *newbuf = realloc(ci->data, ci->size + *upload_data_size + 1);
        if (!newbuf) {
            free(ci->data);
            free(ci);
            *con_cls = NULL;
            return MHD_NO;
        }
        ci->data = newbuf;

        memcpy(ci->data + ci->size, upload_data, *upload_data_size);
        ci->size += *upload_data_size;
        ci->data[ci->size] = '\0';

        *upload_data_size = 0;
        return MHD_YES;
    }

    // Parse JSON
    bson_error_t error;
    bson_t *doc = bson_new_from_json((const uint8_t *)ci->data, -1, &error);
    if (!doc) {
        fprintf(stderr, "JSON error: %s\n", error.message);
        free(ci->data);
        free(ci);
        *con_cls = NULL;
        return respond_json(connection, MHD_HTTP_BAD_REQUEST,
                            "{\"status\":\"error\",\"message\":\"invalid json\"}");
    }

    // Append received_at
    char ts[64];
    get_utc_iso8601(ts, sizeof(ts));
    BSON_APPEND_UTF8(doc, "received_at", ts);

    // Mongo defaults (prevents getenv(NULL) crashes)
    const char *mongo_uri = getenv("MONGO_URI");
    const char *mongo_db  = getenv("MONGO_DB");
    const char *mongo_col = getenv("MONGO_COL");
    if (!mongo_uri) mongo_uri = "mongodb://localhost:27017";
    if (!mongo_db)  mongo_db  = "maze";
    if (!mongo_col) mongo_col = "moves";

    mongoc_client_t *client = mongoc_client_new(mongo_uri);
    if (!client) {
        bson_destroy(doc);
        free(ci->data);
        free(ci);
        *con_cls = NULL;
        return respond_json(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                            "{\"status\":\"error\",\"message\":\"mongo client failed\"}");
    }

    mongoc_collection_t *col = mongoc_client_get_collection(client, mongo_db, mongo_col);

    bool ok = mongoc_collection_insert_one(col, doc, NULL, NULL, &error);

    mongoc_collection_destroy(col);
    mongoc_client_destroy(client);
    bson_destroy(doc);

    free(ci->data);
    free(ci);
    *con_cls = NULL;

    if (!ok) {
        fprintf(stderr, "Mongo insert error: %s\n", error.message);
        return respond_json(connection, MHD_HTTP_INTERNAL_SERVER_ERROR,
                            "{\"status\":\"error\",\"message\":\"mongo insert failed\"}");
    }

    return respond_json(connection, MHD_HTTP_OK, "{\"status\":\"ok\"}");
}

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

    if (len <= 0) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)len, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

int main(void) {
    const char *cert_path = getenv("CERT_FILE") ? getenv("CERT_FILE") : "certs/server.crt";
    const char *key_path  = getenv("KEY_FILE")  ? getenv("KEY_FILE")  : "certs/server.key";
    int port = getenv("LISTEN_PORT") ? atoi(getenv("LISTEN_PORT")) : DEFAULT_PORT;

    char *cert_pem = load_file(cert_path);
    char *key_pem  = load_file(key_path);

    if (!cert_pem || !key_pem) {
        fprintf(stderr, "Error: Could not load certificates from %s or %s\n", cert_path, key_path);
        free(cert_pem);
        free(key_pem);
        return 1;
    }

    mongoc_init();

    struct MHD_Daemon *daemon = MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION | MHD_USE_TLS,
        (uint16_t)port,
        NULL, NULL,
        &handle_post, NULL,
        MHD_OPTION_HTTPS_MEM_CERT, cert_pem,
        MHD_OPTION_HTTPS_MEM_KEY,  key_pem,
        MHD_OPTION_END
    );

    if (!daemon) {
        fprintf(stderr, "Failed to start HTTPS server. Check if port %d is open.\n", port);
        mongoc_cleanup();
        free(cert_pem);
        free(key_pem);
        return 1;
    }

    printf("HTTPS server listening on https://0.0.0.0:%d/move\n", port);
    printf("Press ENTER to stop...\n");
    getchar();

    MHD_stop_daemon(daemon);
    mongoc_cleanup();
    free(cert_pem);
    free(key_pem);
    return 0;
}

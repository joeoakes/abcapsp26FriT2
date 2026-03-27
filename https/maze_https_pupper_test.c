#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#define PORT 8449

/* ---------------- Queue ---------------- */

typedef struct Move {
    char dir[32];
    struct Move *next;
} Move;

Move *head = NULL;
Move *tail = NULL;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------------- Connection ---------------- */

struct connection_info {
    char *data;
    size_t size;
};

/* ---------------- Response ---------------- */

static enum MHD_Result respond_json(struct MHD_Connection *connection,
                                    unsigned int status,
                                    const char *body)
{
    struct MHD_Response *resp =
        MHD_create_response_from_buffer(strlen(body),
                                        (void *)body,
                                        MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(connection, status, resp);
    MHD_destroy_response(resp);
    return ret;
}

/* ---------------- Movement ---------------- */

void publish_velocity(float linear, float angular)
{
    time_t start = time(NULL);

    printf("Executing move for 3 seconds: linear=%.2f, angular=%.2f\n",
           linear, angular);

    while (difftime(time(NULL), start) < 3.0) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
            "bash -c 'source /opt/ros/humble/setup.bash && "
            "ros2 topic pub -1 /cmd_vel geometry_msgs/msg/Twist "
            "'\"{\\'linear\\': {\\'x\\': %.2f}, \\'angular\\': {\\'z\\': %.2f}}\"''",
            linear, angular);
        system(cmd);
        usleep(200000); // 200ms between publishes
    }

    // Stop the robot
    printf("Stopping robot\n");
    char stop_cmd[512];
    snprintf(stop_cmd, sizeof(stop_cmd),
        "bash -c 'source /opt/ros/humble/setup.bash && "
        "ros2 topic pub -1 /cmd_vel geometry_msgs/msg/Twist "
        "'\"{\\'linear\\': {\\'x\\': 0.0}, \\'angular\\': {\\'z\\': 0.0}}\"''");
    system(stop_cmd);
}

void process_move(const char *dir)
{
    float linear = 0.0;
    float angular = 0.0;

    if (strcmp(dir, "forward") == 0)
        linear = 0.1;
    else if (strcmp(dir, "backward") == 0)
        linear = -0.1;
    else if (strcmp(dir, "left") == 0)
        angular = 0.5;
    else if (strcmp(dir, "right") == 0)
        angular = -0.5;
    else {
        printf("Invalid direction: %s\n", dir);
        return;
    }

    publish_velocity(linear, angular);
}

/* ---------------- Queue Logic ---------------- */

void enqueue_move(const char *dir)
{
    Move *m = malloc(sizeof(Move));
    strcpy(m->dir, dir);
    m->next = NULL;

    pthread_mutex_lock(&lock);
    if (!tail)
        head = tail = m;
    else {
        tail->next = m;
        tail = m;
    }
    pthread_mutex_unlock(&lock);
}

Move* dequeue_move()
{
    pthread_mutex_lock(&lock);
    Move *m = head;
    if (m) {
        head = head->next;
        if (!head) tail = NULL;
    }
    pthread_mutex_unlock(&lock);
    return m;
}

/* ---------------- Worker Thread ---------------- */

void *worker(void *arg)
{
    while (1) {
        Move *m = dequeue_move();
        if (m) {
            printf("Processing queued move: %s\n", m->dir);
            process_move(m->dir);
            free(m);
        } else {
            usleep(100000); // 100ms idle
        }
    }
    return NULL;
}

/* ---------------- JSON Parsing ---------------- */

void extract_move_dir(const char *json, char *out)
{
    char *key = strstr(json, "\"move_dir\"");
    if (!key) return;
    char *colon = strchr(key, ':');
    if (!colon) return;
    char *first_quote = strchr(colon, '\"');
    if (!first_quote) return;
    first_quote++;
    char *second_quote = strchr(first_quote, '\"');
    if (!second_quote) return;
    size_t len = second_quote - first_quote;
    strncpy(out, first_quote, len);
    out[len] = '\0';
}

/* ---------------- HTTPS Handler ---------------- */

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

    printf("Received JSON:\n%s\n", ci->data);
    char move_dir[64] = {0};
    extract_move_dir(ci->data, move_dir);

    if (strlen(move_dir) > 0) {
        printf("Queued move_dir: %s\n", move_dir);
        enqueue_move(move_dir);
    } else {
        printf("move_dir not found\n");
    }

    free(ci->data);
    free(ci);
    *con_cls = NULL;

    return respond_json(connection, MHD_HTTP_OK, "{\"status\":\"queued\"}");
}

/* ---------------- TLS ---------------- */

static char *load_file(const char *filename)
{
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

/* ---------------- MAIN ---------------- */

int main()
{
    char *cert = load_file("certs/server.crt");
    char *key  = load_file("certs/server.key");

    if (!cert || !key) {
        printf("Failed to load TLS files\n");
        return 1;
    }

    pthread_t tid;
    pthread_create(&tid, NULL, worker, NULL);

    struct MHD_Daemon *daemon =
        MHD_start_daemon(
            MHD_USE_THREAD_PER_CONNECTION | MHD_USE_TLS,
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

    printf("Mini Pupper HTTPS control running at:\nhttps://localhost:%d\n", PORT);

    while (1)
        sleep(1);

    MHD_stop_daemon(daemon);
    free(cert);
    free(key);
    return 0;
}

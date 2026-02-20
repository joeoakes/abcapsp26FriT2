#include <microhttpd.h>
#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <geometry_msgs/msg/twist.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PORT 8448
#define POSTBUFFERSIZE 65536

// Global ROS 2 Handles
rcl_publisher_t publisher;
geometry_msgs__msg__Twist msg;

struct connection_info {
    char *data;
    size_t size;
};

// Helper to respond with JSON
static int respond_json(struct MHD_Connection *connection, unsigned int status, const char *body) {
    struct MHD_Response *resp = MHD_create_response_from_buffer(strlen(body), (void *)body, MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(resp, "Content-Type", "application/json");
    int ret = MHD_queue_response(connection, status, resp);
    MHD_destroy_response(resp);
    return ret;
}

// POST Handler: This is where the CURL command lands
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

    // JSON Logic: Here we trigger the ROS movement
    printf("Received Command: %s\n", ci->data);

    // Set movement (e.g., move forward)
    msg.linear.x = 0.2;  // meters per second
    msg.angular.z = 0.0; 

    // PUBLISH to ROS 2
    rcl_ret_t rc = rcl_publish(&publisher, &msg, NULL);

    if (rc == RCL_RET_OK) {
        return respond_json(connection, MHD_HTTP_OK, "{\"status\":\"success\", \"message\":\"Pupper moving\"}");
    } else {
        return respond_json(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "{\"status\":\"error\"}");
    }
}

// Basic file loader for Certs
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

int main(int argc, const char * const * argv) {
    // 1. Initialize ROS 2
    rcl_allocator_t allocator = rcl_get_default_allocator();
    rclc_support_t support;
    rclc_support_init(&support, argc, argv, &allocator);

    rcl_node_t node = rcl_get_zero_initialized_node();
    rclc_node_init_default(&node, "pupper_https_bridge", "", &support);

    // 2. Initialize Publisher
    rclc_publisher_init_default(&publisher, &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist), "cmd_vel");

    // 3. Load Certs
    char *cert = load_file("certs/server.crt");
    char *key = load_file("certs/server.key");

    // 4. Start HTTPS Server
    struct MHD_Daemon *daemon = MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION | MHD_USE_TLS, PORT,
        NULL, NULL, &handle_post, NULL,
        MHD_OPTION_HTTPS_MEM_CERT, cert,
        MHD_OPTION_HTTPS_MEM_KEY, key,
        MHD_OPTION_END);

    if (!daemon) return 1;

    printf("Pupper ROS Bridge active on https://localhost:%d\n", PORT);

    // 5. Keep alive (ROS Spin)
    while (rcl_context_is_valid(&support.context)) {
        sleep(1); 
    }

    // Cleanup
    MHD_stop_daemon(daemon);
    rcl_publisher_fini(&publisher, &node);
    rcl_node_fini(&node);
    return 0;
}

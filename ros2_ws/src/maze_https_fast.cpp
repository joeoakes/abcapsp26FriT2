#include <microhttpd.h>
#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <memory>
#include <chrono>

#define PORT 8449

struct connection_info {
    std::string data;
};

static std::shared_ptr<rclcpp::Node> g_node;
static rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr g_pub;

/* ---------------- LOAD FILE ---------------- */

static char *load_file(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f) return nullptr;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc(len + 1);
    if (!buf) {
        fclose(f);
        return nullptr;
    }
fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    return buf;
}

/* ---------------- RESPONSE ---------------- */

static enum MHD_Result respond_json(struct MHD_Connection *connection,
                                    unsigned int status,
                                    const char *body)
{
    struct MHD_Response *resp =
        MHD_create_response_from_buffer(strlen(body),
                                        (void *)body,
                                        MHD_RESPMEM_MUST_COPY);

    if (!resp)
        return MHD_NO;

    MHD_add_response_header(resp, "Content-Type", "application/json");

    enum MHD_Result ret = MHD_queue_response(connection, status, resp);
    MHD_destroy_response(resp);
    return ret;
}

/* ---------------- JSON PARSE ---------------- */

static bool extract_move_dir(const std::string &json, std::string &out)
{
    size_t key = json.find("\"move_dir\"");
    if (key == std::string::npos) return false;

    size_t colon = json.find(':', key);
    if (colon == std::string::npos) return false;
    MHD_start_daemon(
            MHD_USE_THREAD_PER_CONNECTION | MHD_USE_TLS,
            PORT,
            nullptr, nullptr,
            &handle_post, nullptr,
            MHD_OPTION_HTTPS_MEM_CERT, cert,
            MHD_OPTION_HTTPS_MEM_KEY, key,
            MHD_OPTION_END);

    if (!daemon) {
        std::printf("Failed to start HTTPS server\n");
        free(cert);
        free(key);
        rclcpp::shutdown();
        if (ros_thread.joinable()) ros_thread.join();
        return 1;
    }

    std::printf("🚀 Fast Mini Pupper HTTPS running at:\n");
    std::printf("https://localhost:%d\n", PORT);

    while (rclcpp::ok()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    MHD_stop_daemon(daemon);
    free(cert);
    free(key);

    rclcpp::shutdown();
    if (ros_thread.joinable()) ros_thread.join();

    return 0;
}

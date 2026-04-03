#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <curl/curl.h>

#define MAZE_W 21
#define MAZE_H 15
#define CELL 32
#define PAD 16

#define PUPPER_URL "https://10.170.8.119:8449/"
#define TELEMETRY_URL "http://10.170.8.109/maze_bridge.php"
#define FRAME_PATH "/tmp/maze_latest.bmp"

enum { WALL_N = 1, WALL_E = 2, WALL_S = 4, WALL_W = 8 };
enum { DIR_NORTH = 0, DIR_EAST = 1, DIR_SOUTH = 2, DIR_WEST = 3 };

typedef struct { uint8_t walls; bool visited; } Cell;
typedef struct { int x, y; } Node;

static Cell g[MAZE_H][MAZE_W];
static int move_sequence = 0;
static int robot_heading = DIR_NORTH;
static bool astar_running = false;

/* ---------------- HTTP/HTTPS ---------------- */

static int https_post_json(const char *url, const char *json) {
    CURL *curl = curl_easy_init();
    if (!curl) return 0;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "HTTPS curl error: %s\n", curl_easy_strerror(res));
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK);
}

static int http_post_json(const char *url, const char *json) {
    CURL *curl = curl_easy_init();
    if (!curl) return 0;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "HTTP telemetry error: %s\n", curl_easy_strerror(res));
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK);
}

static void send_pupper_move(const char *dir) {
    char json[128];
    snprintf(json, sizeof(json), "{\"move_dir\":\"%s\"}", dir);
    printf("Sending to pupper: %s\n", json);
    https_post_json(PUPPER_URL, json);
}

static void send_maze_telemetry(
    const char *event_type,
    int px,
    int py,
    const char *move_dir,
    bool goal_reached,
    int level,
    bool astar_is_running,
    int path_length,
    int path_index
) {
    char json[1024];
    time_t now = time(NULL);

    snprintf(json, sizeof(json),
        "{"
            "\"event_type\":\"%s\","
            "\"timestamp\":\"%ld\","
            "\"player\":{\"position\":{\"x\":%d,\"y\":%d}},"
            "\"goal_reached\":%s,"
            "\"level\":%d,"
            "\"input\":{"
                "\"move_sequence\":%d,"
                "\"move_dir\":\"%s\""
            "},"
            "\"robot\":{"
                "\"heading\":%d,"
                "\"astar_running\":%s,"
                "\"path_length\":%d,"
                "\"path_index\":%d"
            "}"
        "}",
        event_type,
        (long)now,
        px,
        py,
        goal_reached ? "true" : "false",
        level,
        move_sequence,
        move_dir ? move_dir : "",
        robot_heading,
        astar_is_running ? "true" : "false",
        path_length,
        path_index
    );

    printf("Sending telemetry: %s\n", json);
    http_post_json(TELEMETRY_URL, json);
}

/* ---------------- Stream Frame Save ---------------- */

static void save_maze_frame(SDL_Renderer *r, const char *filename) {
    int width = PAD * 2 + MAZE_W * CELL;
    int height = PAD * 2 + MAZE_H * CELL;

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
        0, width, height, 32, SDL_PIXELFORMAT_ARGB8888
    );
    if (!surface) {
        fprintf(stderr, "SDL_CreateRGBSurfaceWithFormat failed: %s\n", SDL_GetError());
        return;
    }

    if (SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) != 0) {
        fprintf(stderr, "SDL_RenderReadPixels failed: %s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        return;
    }

    if (SDL_SaveBMP(surface, filename) != 0) {
        fprintf(stderr, "SDL_SaveBMP failed: %s\n", SDL_GetError());
    }

    SDL_FreeSurface(surface);
}

/* ---------------- Robot Orientation Helpers ---------------- */

static void turn_left_and_update(void) {
    send_pupper_move("left");
    SDL_Delay(250);
    robot_heading = (robot_heading + 3) % 4;
}

static void turn_right_and_update(void) {
    send_pupper_move("right");
    SDL_Delay(250);
    robot_heading = (robot_heading + 1) % 4;
}

static void move_forward_step(void) {
    send_pupper_move("forward");
    SDL_Delay(250);
}

static void move_backward_step(void) {
    send_pupper_move("backward");
    SDL_Delay(250);
}

static int desired_heading_from_step(int oldx, int oldy, int newx, int newy) {
    if (newx == oldx && newy == oldy - 1) return DIR_NORTH;
    if (newx == oldx + 1 && newy == oldy) return DIR_EAST;
    if (newx == oldx && newy == oldy + 1) return DIR_SOUTH;
    if (newx == oldx - 1 && newy == oldy) return DIR_WEST;
    return -1;
}

static void orient_and_move_forward(int oldx, int oldy, int newx, int newy) {
    int desired = desired_heading_from_step(oldx, oldy, newx, newy);
    if (desired == -1) return;

    int diff = (desired - robot_heading + 4) % 4;
    if (diff == 1) turn_right_and_update();
    else if (diff == 2) {
        turn_right_and_update();
        turn_right_and_update();
    }
    else if (diff == 3) turn_left_and_update();

    move_forward_step();
}

/* ---------------- Maze Helpers ---------------- */

static inline bool in_bounds(int x, int y) {
    return x >= 0 && x < MAZE_W && y >= 0 && y < MAZE_H;
}

static void knock_down(int x, int y, int nx, int ny) {
    if (nx == x && ny == y - 1) {
        g[y][x].walls &= ~WALL_N;
        g[ny][nx].walls &= ~WALL_S;
    }
    else if (nx == x + 1 && ny == y) {
        g[y][x].walls &= ~WALL_E;
        g[ny][nx].walls &= ~WALL_W;
    }
    else if (nx == x && ny == y + 1) {
        g[y][x].walls &= ~WALL_S;
        g[ny][nx].walls &= ~WALL_N;
    }
    else if (nx == x - 1 && ny == y) {
        g[y][x].walls &= ~WALL_W;
        g[ny][nx].walls &= ~WALL_E;
    }
}

static void maze_init(void) {
    for (int y = 0; y < MAZE_H; y++) {
        for (int x = 0; x < MAZE_W; x++) {
            g[y][x].walls = WALL_N | WALL_E | WALL_S | WALL_W;
            g[y][x].visited = false;
        }
    }
}

static void maze_generate(int sx, int sy) {
    typedef struct { int x, y; } P;
    P stack[MAZE_W * MAZE_H];
    int top = 0;

    g[sy][sx].visited = true;
    stack[top++] = (P){sx, sy};

    int dx[4] = {0, 1, 0, -1};
    int dy[4] = {-1, 0, 1, 0};

    while (top > 0) {
        P c = stack[top - 1];
        int n = 0;
        P opts[4];

        for (int i = 0; i < 4; i++) {
            int nx = c.x + dx[i];
            int ny = c.y + dy[i];
            if (in_bounds(nx, ny) && !g[ny][nx].visited) {
                opts[n++] = (P){nx, ny};
            }
        }

        if (n == 0) {
            top--;
            continue;
        }

        P next = opts[rand() % n];
        knock_down(c.x, c.y, next.x, next.y);
        g[next.y][next.x].visited = true;
        stack[top++] = next;
    }

    for (int y = 0; y < MAZE_H; y++) {
        for (int x = 0; x < MAZE_W; x++) {
            g[y][x].visited = false;
        }
    }
}

/* ---------------- Player Move ---------------- */

static bool try_move(int *px, int *py, int dx, int dy) {
    int x = *px;
    int y = *py;
    int nx = x + dx;
    int ny = y + dy;

    if (!in_bounds(nx, ny)) return false;

    uint8_t w = g[y][x].walls;
    if ((dx == 0 && dy == -1 && (w & WALL_N)) ||
        (dx == 1 && dy == 0 && (w & WALL_E)) ||
        (dx == 0 && dy == 1 && (w & WALL_S)) ||
        (dx == -1 && dy == 0 && (w & WALL_W))) {
        return false;
    }

    *px = nx;
    *py = ny;
    return true;
}

/* ---------------- A* Solver ---------------- */

static int heuristic(int x, int y) {
    return abs(x - (MAZE_W - 1)) + abs(y - (MAZE_H - 1));
}

static int astar(Node path[], int sx, int sy) {
    typedef struct { int g, f, parent; bool open, closed; } ANode;

    ANode nodes[MAZE_W * MAZE_H];
    int open_list[MAZE_W * MAZE_H];
    int open_count = 0;

    for (int i = 0; i < MAZE_W * MAZE_H; i++) {
        nodes[i].g = 1000000;
        nodes[i].f = 1000000;
        nodes[i].parent = -1;
        nodes[i].open = false;
        nodes[i].closed = false;
    }

    int start = sy * MAZE_W + sx;
    int goal = (MAZE_H - 1) * MAZE_W + (MAZE_W - 1);

    nodes[start].g = 0;
    nodes[start].f = heuristic(sx, sy);
    nodes[start].open = true;
    open_list[open_count++] = start;

    int dx[4] = {0, 1, 0, -1};
    int dy[4] = {-1, 0, 1, 0};
    int masks[4] = {WALL_N, WALL_E, WALL_S, WALL_W};

    while (open_count > 0) {
        int best = 0;
        for (int i = 1; i < open_count; i++) {
            if (nodes[open_list[i]].f < nodes[open_list[best]].f) {
                best = i;
            }
        }

        int current = open_list[best];
        open_list[best] = open_list[--open_count];
        nodes[current].open = false;
        nodes[current].closed = true;

        if (current == goal) break;

        int cx = current % MAZE_W;
        int cy = current / MAZE_W;

        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d];
            int ny = cy + dy[d];

            if (!in_bounds(nx, ny) || (g[cy][cx].walls & masks[d])) continue;

            int ni = ny * MAZE_W + nx;
            if (nodes[ni].closed) continue;

            int ng = nodes[current].g + 1;
            if (!nodes[ni].open || ng < nodes[ni].g) {
                nodes[ni].g = ng;
                nodes[ni].f = ng + heuristic(nx, ny);
                nodes[ni].parent = current;

                if (!nodes[ni].open) {
                    nodes[ni].open = true;
                    open_list[open_count++] = ni;
                }
            }
        }
    }

    if (nodes[goal].parent == -1 && goal != start) return 0;

    int idx = goal;
    int len = 0;
    while (idx != -1) {
        path[len++] = (Node){idx % MAZE_W, idx / MAZE_W};
        idx = nodes[idx].parent;
    }

    for (int i = 0; i < len / 2; i++) {
        Node t = path[i];
        path[i] = path[len - i - 1];
        path[len - i - 1] = t;
    }

    return len;
}

/* ---------------- Drawing ---------------- */

static void draw(SDL_Renderer *r, int px, int py) {
    SDL_SetRenderDrawColor(r, 15, 15, 18, 255);
    SDL_RenderClear(r);

    SDL_SetRenderDrawColor(r, 230, 230, 230, 255);
    for (int y = 0; y < MAZE_H; y++) {
        for (int x = 0; x < MAZE_W; x++) {
            int ox = PAD + x * CELL;
            int oy = PAD + y * CELL;
            int ex = ox + CELL;
            int ey = oy + CELL;
            uint8_t w = g[y][x].walls;

            if (w & WALL_N) SDL_RenderDrawLine(r, ox, oy, ex, oy);
            if (w & WALL_E) SDL_RenderDrawLine(r, ex, oy, ex, ey);
            if (w & WALL_S) SDL_RenderDrawLine(r, ox, ey, ex, ey);
            if (w & WALL_W) SDL_RenderDrawLine(r, ox, oy, ox, ey);
        }
    }

    SDL_Rect goal = {
        PAD + (MAZE_W - 1) * CELL + 6,
        PAD + (MAZE_H - 1) * CELL + 6,
        CELL - 12,
        CELL - 12
    };
    SDL_SetRenderDrawColor(r, 40, 160, 70, 255);
    SDL_RenderFillRect(r, &goal);

    SDL_Rect player = {
        PAD + px * CELL + 8,
        PAD + py * CELL + 8,
        CELL - 16,
        CELL - 16
    };
    SDL_SetRenderDrawColor(r, 255, 215, 0, 255);
    SDL_RenderFillRect(r, &player);

    SDL_RenderPresent(r);
    save_maze_frame(r, FRAME_PATH);
}

/* ---------------- Main ---------------- */

int main(void) {
    srand((unsigned int)time(NULL));
    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "Maze + Mini Pupper Control",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        PAD * 2 + MAZE_W * CELL,
        PAD * 2 + MAZE_H * CELL,
        SDL_WINDOW_SHOWN
    );
    if (!win) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!r) {
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    maze_init();
    maze_generate(0, 0);

    int px = 0;
    int py = 0;
    bool running = true;

    Node path[MAZE_W * MAZE_H];
    int path_len = 0;
    int path_idx = 0;

    draw(r, px, py);
    send_maze_telemetry("startup", px, py, "startup", false, 1, astar_running, path_len, path_idx);

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            }

            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;

                if (k == SDLK_ESCAPE) {
                    running = false;
                }

                if (k == SDLK_r) {
                    maze_init();
                    maze_generate(0, 0);
                    px = 0;
                    py = 0;
                    move_sequence = 0;
                    robot_heading = DIR_NORTH;
                    astar_running = false;
                    path_len = 0;
                    path_idx = 0;

                    draw(r, px, py);
                    send_maze_telemetry("reset", px, py, "reset", false, 1, astar_running, path_len, path_idx);
                }

                if (k == SDLK_q) {
                    if (!astar_running) {
                        path_len = astar(path, px, py);
                        path_idx = 1;
                        astar_running = true;
                        send_maze_telemetry("astar_start", px, py, "astar", false, 1, astar_running, path_len, path_idx);
                    } else {
                        astar_running = false;
                        send_pupper_move("stop");
                        send_maze_telemetry("astar_stop", px, py, "stop", false, 1, astar_running, path_len, path_idx);
                    }
                }

                int dx = 0;
                int dy = 0;

                if (k == SDLK_w || k == SDLK_UP) dy = -1;
                else if (k == SDLK_s || k == SDLK_DOWN) dy = 1;
                else if (k == SDLK_a || k == SDLK_LEFT) dx = -1;
                else if (k == SDLK_d || k == SDLK_RIGHT) dx = 1;

                if ((dx != 0 || dy != 0) && !astar_running) {
                    int oldx = px;
                    int oldy = py;

                    if (try_move(&px, &py, dx, dy)) {
                        int desired = desired_heading_from_step(oldx, oldy, px, py);
                        bool goal_reached = (px == MAZE_W - 1 && py == MAZE_H - 1);

                        if (desired == robot_heading) {
                            move_sequence++;
                            move_forward_step();
                            draw(r, px, py);
                            send_maze_telemetry("manual_move", px, py, "forward", goal_reached, 1, astar_running, path_len, path_idx);
                        }
                        else if (((desired + 2) % 4) == robot_heading) {
                            move_sequence++;
                            move_backward_step();
                            draw(r, px, py);
                            send_maze_telemetry("manual_move", px, py, "backward", goal_reached, 1, astar_running, path_len, path_idx);
                        }
                        else {
                            orient_and_move_forward(oldx, oldy, px, py);
                            move_sequence++;
                            draw(r, px, py);
                            send_maze_telemetry("manual_move", px, py, "forward", goal_reached, 1, astar_running, path_len, path_idx);
                        }
                    }
                }

                if (k == SDLK_SPACE) {
                    send_pupper_move("stop");
                    send_maze_telemetry("stop", px, py, "stop", false, 1, astar_running, path_len, path_idx);
                }
            }
        }

        if (astar_running && path_idx < path_len) {
            int oldx = px;
            int oldy = py;

            px = path[path_idx].x;
            py = path[path_idx].y;

            orient_and_move_forward(oldx, oldy, px, py);
            move_sequence++;

            bool goal_reached = (px == MAZE_W - 1 && py == MAZE_H - 1);
            draw(r, px, py);
            send_maze_telemetry("astar_step", px, py, "forward", goal_reached, 1, astar_running, path_len, path_idx);

            SDL_Delay(3000);
            path_idx++;
        }
        else if (astar_running && path_idx >= path_len) {
            send_pupper_move("stop");
            astar_running = false;
            send_maze_telemetry("astar_complete", px, py, "complete", true, 1, astar_running, path_len, path_idx);
        }

        draw(r, px, py);
        SDL_Delay(16);
    }

    send_maze_telemetry("shutdown", px, py, "shutdown", false, 1, astar_running, path_len, path_idx);

    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    curl_global_cleanup();
    return 0;
}

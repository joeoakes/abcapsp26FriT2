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

// ROBOT CONTROL ENDPOINT (FIXED PORT)
#define ROBOT_URL "https://10.170.8.130:8449"

// LOGGING ENDPOINT (keep if you want)
#define MOVE_URL "https://10.170.8.130:8448/move"

enum { WALL_N = 1, WALL_E = 2, WALL_S = 4, WALL_W = 8 };

typedef struct {
    uint8_t walls;
    bool visited;
} Cell;

static Cell g[MAZE_H][MAZE_W];

typedef struct { int x, y; } Node;

static int level_index = 0;
static int move_sequence = 0;

/* ---------------- HTTPS ---------------- */

static int https_post_json(const char *url, const char *json) {
    CURL *curl = curl_easy_init();
    if (!curl) return 0;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);

    // Allow self-signed cert
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK);
}

/* ---------------- SEND ROBOT COMMAND ---------------- */
void send_move_command(const char *dir)
{
    char json[128];
    snprintf(json, sizeof(json),
        "{\"move_dir\":\"%s\"}", dir);

    printf("Sending robot command: %s\n", json);
    https_post_json(ROBOT_URL, json);
}

/* ---------------- LOGGING ---------------- */

static void iso_utc_now(char out[32]) {
    time_t t = time(NULL);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    strftime(out, 32, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

static void send_event(const char *type, int x, int y, bool goal) {
    char ts[32];
    iso_utc_now(ts);

    char json[512];
    snprintf(json, sizeof(json),
        "{\"event_type\":\"%s\",\"level\":%d,\"input\":{\"device\":\"keyboard\",\"move_sequence\":%d},"
        "\"player\":{\"position\":{\"x\":%d,\"y\":%d}},\"goal_reached\":%s,\"timestamp\":\"%s\"}",
        type, level_index, move_sequence, x, y, goal ? "true" : "false", ts);

    https_post_json(MOVE_URL, json);
}

/* ---------------- Maze ---------------- */

static inline bool in_bounds(int x, int y) { return x >= 0 && x < MAZE_W && y >= 0 && y < MAZE_H; }

static void knock_down(int x, int y, int nx, int ny) {
    if (nx == x && ny == y - 1) { g[y][x].walls &= ~WALL_N; g[ny][nx].walls &= ~WALL_S; }
    else if (nx == x + 1 && ny == y) { g[y][x].walls &= ~WALL_E; g[ny][nx].walls &= ~WALL_W; }
    else if (nx == x && ny == y + 1) { g[y][x].walls &= ~WALL_S; g[ny][nx].walls &= ~WALL_N; }
    else if (nx == x - 1 && ny == y) { g[y][x].walls &= ~WALL_W; g[ny][nx].walls &= ~WALL_E; }
}

static void maze_init() {
    for (int y = 0; y < MAZE_H; y++)
        for (int x = 0; x < MAZE_W; x++) {
            g[y][x].walls = WALL_N | WALL_E | WALL_S | WALL_W;
            g[y][x].visited = false;
        }
}

static void maze_generate(int sx, int sy) {
    typedef struct { int x, y; } P;
    P stack[MAZE_W * MAZE_H];
    int top = 0;
    g[sy][sx].visited = true;
    stack[top++] = (P){sx, sy};

    int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};

    while (top > 0) {
        P c = stack[top - 1];
        int n = 0; P opts[4];

        for (int i = 0; i < 4; i++) {
            int nx = c.x + dx[i], ny = c.y + dy[i];
            if (in_bounds(nx, ny) && !g[ny][nx].visited)
                opts[n++] = (P){nx, ny};
        }

        if (n == 0) { top--; continue; }

        P next = opts[rand() % n];
        knock_down(c.x, c.y, next.x, next.y);

        g[next.y][next.x].visited = true;
        stack[top++] = next;
    }
}

/* ---------------- A* ---------------- */

static int heuristic(int x, int y) {
    return abs(x - (MAZE_W - 1)) + abs(y - (MAZE_H - 1));
}

static int astar(Node path[MAZE_W * MAZE_H], int sx, int sy) {
    typedef struct { int g, f, p; bool c; } N;
    N n[MAZE_W * MAZE_H];

    for (int i = 0; i < MAZE_W * MAZE_H; i++) {
        n[i].g = 999999; n[i].f = 999999; n[i].p = -1; n[i].c = false;
    }

    int open[MAZE_W * MAZE_H], oc = 0;
    int s = sy * MAZE_W + sx;

    n[s].g = 0;
    n[s].f = heuristic(sx, sy);
    open[oc++] = s;

    int goal = (MAZE_H - 1) * MAZE_W + (MAZE_W - 1);

    while (oc > 0) {
        int best = 0;
        for (int i = 1; i < oc; i++)
            if (n[open[i]].f < n[open[best]].f) best = i;

        int cur = open[best];
        open[best] = open[--oc];

        if (cur == goal) break;

        n[cur].c = true;

        int cx = cur % MAZE_W, cy = cur / MAZE_W;

        int dx[4] = {0,1,0,-1}, dy[4] = {-1,0,1,0};
        uint8_t m[4] = {WALL_N, WALL_E, WALL_S, WALL_W};

        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d], ny = cy + dy[d];

            if (!in_bounds(nx, ny) || (g[cy][cx].walls & m[d])) continue;

            int ni = ny * MAZE_W + nx;

            if (n[ni].c) continue;

            int ng = n[cur].g + 1;

            if (ng < n[ni].g) {
                n[ni].g = ng;
                n[ni].f = ng + heuristic(nx, ny);
                n[ni].p = cur;
                open[oc++] = ni;
            }
        }
    }

    int idx = goal, len = 0;

    while (idx != -1) {
        path[len++] = (Node){idx % MAZE_W, idx / MAZE_W};
        idx = n[idx].p;
    }

    for (int i = 0; i < len / 2; i++) {
        Node t = path[i];
        path[i] = path[len - i - 1];
        path[len - i - 1] = t;
    }

    return len;
}

/* ---------------- Main ---------------- */

int main() {
    srand(time(NULL));
    curl_global_init(CURL_GLOBAL_DEFAULT);
    SDL_Init(SDL_INIT_VIDEO);

    SDL_Window *w = SDL_CreateWindow("Maze Robot",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        PAD*2 + MAZE_W*CELL, PAD*2 + MAZE_H*CELL, 0);

    SDL_Renderer *r = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED);

    maze_init();
    maze_generate(0,0);

    int px=0, py=0;
    bool run=true;

    while (run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) run=false;

            if (e.type == SDL_KEYDOWN) {
                int dx=0, dy=0;

                if (e.key.keysym.sym==SDLK_w || e.key.keysym.sym==SDLK_UP) dy=-1;
                if (e.key.keysym.sym==SDLK_s || e.key.keysym.sym==SDLK_DOWN) dy=1;
                if (e.key.keysym.sym==SDLK_a || e.key.keysym.sym==SDLK_LEFT) dx=-1;
                if (e.key.keysym.sym==SDLK_d || e.key.keysym.sym==SDLK_RIGHT) dx=1;

                if (try_move(&px,&py,dx,dy)) {

                    if (dy==-1) send_move_command("forward");
                    if (dy==1) send_move_command("backward");
                    if (dx==-1) send_move_command("left");
                    if (dx==1) send_move_command("right");

                    move_sequence++;
                    bool goal = (px==MAZE_W-1 && py==MAZE_H-1);
                    send_event("player_move", px, py, goal);
                }
            }
        }

        draw(r,px,py);
    }

    SDL_Quit();
    curl_global_cleanup();
    return 0;
}

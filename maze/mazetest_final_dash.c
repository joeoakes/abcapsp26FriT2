#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <curl/curl.h>

/* ---------------- Function prototypes ---------------- */
bool try_move(int *px, int *py, int dx, int dy);
void draw(SDL_Renderer *r, int px, int py);

#define MAZE_W 21
#define MAZE_H 15
#define CELL 32
#define PAD 16

// ROBOT CONTROL ENDPOINT
#define ROBOT_URL "https://10.170.8.130:8449"

// LOGGING ENDPOINT (original)
#define MOVE_URL "https://10.170.8.130:8448/move"

// DASHBOARD BRIDGE (your web server)
#define DASHBOARD_URL "https://my.up.ist.psu.edu/jcc6088/440W/maze_bridge.php"

enum { WALL_N = 1, WALL_E = 2, WALL_S = 4, WALL_W = 8 };

typedef struct {
    uint8_t walls;
    bool visited;
} Cell;

static Cell g[MAZE_H][MAZE_W];

typedef struct { int x, y; } Node;

static int level_index = 0;
static int move_sequence = 0;

/* ---------------- HTTPS Functions ---------------- */

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

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK);
}

/* ---------------- Dashboard Telemetry ---------------- */

// Get current UTC time as string
static void get_current_time_iso(char *buffer, size_t size) {
    time_t t = time(NULL);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

// Send telemetry to dashboard PHP bridge
static void send_telemetry_to_dashboard(const char *event_type, int x, int y, bool goal, int move_seq) {
    CURL *curl = curl_easy_init();
    if (!curl) return;
    
    char timestamp[32];
    get_current_time_iso(timestamp, sizeof(timestamp));
    
    char json[512];
    snprintf(json, sizeof(json),
        "{"
        "\"event_type\":\"%s\","
        "\"level\":%d,"
        "\"input\":{\"device\":\"keyboard\",\"move_sequence\":%d},"
        "\"player\":{\"position\":{\"x\":%d,\"y\":%d}},"
        "\"goal_reached\":%s,"
        "\"timestamp\":\"%s\""
        "}",
        event_type, level_index, move_seq, x, y, goal ? "true" : "false", timestamp);
    
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl, CURLOPT_URL, DASHBOARD_URL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L); // Don't block maze if slow
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        printf("Dashboard send failed: %s\n", curl_easy_strerror(res));
    }
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

// Send robot movement command
void send_move_command(const char *dir) {
    char json[128];
    snprintf(json, sizeof(json), "{\"move_dir\":\"%s\"}", dir);
    printf("Sending robot command: %s\n", json);
    https_post_json(ROBOT_URL, json);
}

/* ---------------- Logging (Original) ---------------- */

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
    
    // Send to original HTTPS endpoint
    https_post_json(MOVE_URL, json);
    
    // ALSO send to dashboard bridge
    send_telemetry_to_dashboard(type, x, y, goal, move_sequence);
}

/* ---------------- Maze Generation ---------------- */

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
        int n = 0; 
        P opts[4];

        for (int i = 0; i < 4; i++) {
            int nx = c.x + dx[i], ny = c.y + dy[i];
            if (in_bounds(nx, ny) && !g[ny][nx].visited)
                opts[n++] = (P){nx, ny};
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
}

/* ---------------- Movement & Drawing ---------------- */

bool try_move(int *px, int *py, int dx, int dy) {
    int nx = *px + dx;
    int ny = *py + dy;
    if (!in_bounds(nx, ny)) return false;

    if (dx == 1 && (g[*py][*px].walls & WALL_E)) return false;
    if (dx == -1 && (g[*py][*px].walls & WALL_W)) return false;
    if (dy == 1 && (g[*py][*px].walls & WALL_S)) return false;
    if (dy == -1 && (g[*py][*px].walls & WALL_N)) return false;

    *px = nx;
    *py = ny;
    return true;
}

void draw(SDL_Renderer *r, int px, int py) {
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);

    // Draw maze walls
    for (int y = 0; y < MAZE_H; y++) {
        for (int x = 0; x < MAZE_W; x++) {
            int ox = PAD + x * CELL;
            int oy = PAD + y * CELL;
            SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
            if (g[y][x].walls & WALL_N) 
                SDL_RenderDrawLine(r, ox, oy, ox + CELL, oy);
            if (g[y][x].walls & WALL_S) 
                SDL_RenderDrawLine(r, ox, oy + CELL, ox + CELL, oy + CELL);
            if (g[y][x].walls & WALL_W) 
                SDL_RenderDrawLine(r, ox, oy, ox, oy + CELL);
            if (g[y][x].walls & WALL_E) 
                SDL_RenderDrawLine(r, ox + CELL, oy, ox + CELL, oy + CELL);
        }
    }

    // Draw goal (green square at bottom-right)
    SDL_Rect goal = {
        PAD + (MAZE_W - 1) * CELL + CELL/4, 
        PAD + (MAZE_H - 1) * CELL + CELL/4, 
        CELL/2, 
        CELL/2
    };
    SDL_SetRenderDrawColor(r, 0, 255, 0, 255);
    SDL_RenderFillRect(r, &goal);

    // Draw robot (red square)
    SDL_Rect robot = {
        PAD + px * CELL + CELL/4, 
        PAD + py * CELL + CELL/4, 
        CELL/2, 
        CELL/2
    };
    SDL_SetRenderDrawColor(r, 255, 0, 0, 255);
    SDL_RenderFillRect(r, &robot);

    SDL_RenderPresent(r);
}

/* ---------------- Main ---------------- */

int main(int argc, char *argv[]) {
    srand(time(NULL));
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *w = SDL_CreateWindow("Maze Robot - Telemetry to Dashboard",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        PAD * 2 + MAZE_W * CELL, PAD * 2 + MAZE_H * CELL, 
        SDL_WINDOW_SHOWN);

    if (!w) {
        printf("Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *r = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED);
    if (!r) {
        printf("Renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(w);
        SDL_Quit();
        return 1;
    }

    printf("Maze Robot Started!\n");
    printf("Controls: WASD or Arrow Keys to move\n");
    printf("Sending telemetry to dashboard at: %s\n", DASHBOARD_URL);
    printf("Also sending to original endpoint: %s\n", MOVE_URL);
    printf("Robot control endpoint: %s\n\n", ROBOT_URL);

    maze_init();
    maze_generate(0, 0);

    int px = 0, py = 0;
    bool running = true;
    bool goal_reached = false;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            }

            if (e.type == SDL_KEYDOWN) {
                int dx = 0, dy = 0;

                if (e.key.keysym.sym == SDLK_w || e.key.keysym.sym == SDLK_UP) dy = -1;
                if (e.key.keysym.sym == SDLK_s || e.key.keysym.sym == SDLK_DOWN) dy = 1;
                if (e.key.keysym.sym == SDLK_a || e.key.keysym.sym == SDLK_LEFT) dx = -1;
                if (e.key.keysym.sym == SDLK_d || e.key.keysym.sym == SDLK_RIGHT) dx = 1;

                if (try_move(&px, &py, dx, dy)) {
                    // Send robot control command
                    if (dy == -1) send_move_command("forward");
                    if (dy == 1) send_move_command("backward");
                    if (dx == -1) send_move_command("left");
                    if (dx == 1) send_move_command("right");

                    // Update telemetry
                    move_sequence++;
                    goal_reached = (px == MAZE_W - 1 && py == MAZE_H - 1);
                    
                    // Send event to both original endpoint and dashboard
                    send_event("player_move", px, py, goal_reached);
                    
                    printf("Move #%d: Position (%d, %d) | Goal: %s\n", 
                           move_sequence, px, py, goal_reached ? "REACHED!" : "No");
                    
                    if (goal_reached) {
                        printf("\n🎉 GOAL REACHED! 🎉\n");
                        printf("Total moves: %d\n", move_sequence);
                    }
                }
                
                // Reset maze on 'R' key
                if (e.key.keysym.sym == SDLK_r) {
                    printf("\nResetting maze...\n");
                    maze_init();
                    maze_generate(0, 0);
                    px = 0;
                    py = 0;
                    goal_reached = false;
                    send_event("reset", px, py, false);
                    printf("Maze reset. Starting from (0,0)\n");
                }
            }
        }

        draw(r, px, py);
        SDL_Delay(16); // ~60 FPS
    }

    // Cleanup
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
    SDL_Quit();
    curl_global_cleanup();
    
    printf("\nMaze Robot Shutdown. Total moves made: %d\n", move_sequence);
    
    return 0;
}

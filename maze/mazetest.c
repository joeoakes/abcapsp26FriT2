// maze_sdl2_mongo.c
// SDL2 Maze that logs moves to remote MongoDB

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <mongoc/mongoc.h>

#define MAZE_W 21
#define MAZE_H 15
#define CELL   32
#define PAD    16

enum { WALL_N = 1, WALL_E = 2, WALL_S = 4, WALL_W = 8 };

typedef struct {
    uint8_t walls;
    bool visited;
} Cell;

static Cell g[MAZE_H][MAZE_W];

/* ---------------- MongoDB ---------------- */

static mongoc_client_t *client = NULL;
static mongoc_collection_t *collection = NULL;

static int run_level = 0;

typedef struct {
    int x;
    int y;
} Move;

#define MAX_MOVES 10000
static Move move_buffer[MAX_MOVES];
static int move_count = 0;

static void mongo_start_level(int start_x, int start_y) {
    run_level++;
    move_count = 0;
    move_buffer[move_count++] = (Move){start_x, start_y};
}

static void mongo_append_move(int x, int y) {
    if (move_count < MAX_MOVES) {
        move_buffer[move_count++] = (Move){x, y};
    }
}

static void mongo_finish_level(int px, int py) {
    if (run_level == 0) return;

    bson_t *doc = bson_new();
    bson_error_t error;

    time_t now = time(NULL);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", localtime(&now));

    BSON_APPEND_UTF8(doc, "created_at", time_str);
    BSON_APPEND_INT32(doc, "level", run_level);

    bson_t moves;
    BSON_APPEND_ARRAY_BEGIN(doc, "moves", &moves);

    for (int i = 0; i < move_count; i++) {
        bson_t move;
        char key[16];
        bson_uint32_to_string(i, NULL, key, sizeof(key));

        BSON_APPEND_DOCUMENT_BEGIN(&moves, key, &move);
        BSON_APPEND_INT32(&move, "x", move_buffer[i].x);
        BSON_APPEND_INT32(&move, "y", move_buffer[i].y);
        bson_append_document_end(&moves, &move);
    }

    bson_append_array_end(doc, &moves);

    bson_t current;
    BSON_APPEND_DOCUMENT_BEGIN(doc, "current", &current);
    BSON_APPEND_INT32(&current, "x", px);
    BSON_APPEND_INT32(&current, "y", py);
    bson_append_document_end(doc, &current);

    if (!mongoc_collection_insert_one(collection, doc, NULL, NULL, &error)) {
        fprintf(stderr, "Mongo insert failed: %s\n", error.message);
    } else {
        printf("Level %d saved to MongoDB\n", run_level);
    }

    bson_destroy(doc);
}

/* ---------------- Maze Logic ---------------- */

static inline bool in_bounds(int x, int y) {
    return x >= 0 && x < MAZE_W && y >= 0 && y < MAZE_H;
}

static void knock_down(int x, int y, int nx, int ny) {
    if (nx == x && ny == y - 1) {
        g[y][x].walls &= ~WALL_N;
        g[ny][nx].walls &= ~WALL_S;
    } else if (nx == x + 1 && ny == y) {
        g[y][x].walls &= ~WALL_E;
        g[ny][nx].walls &= ~WALL_W;
    } else if (nx == x && ny == y + 1) {
        g[y][x].walls &= ~WALL_S;
        g[ny][nx].walls &= ~WALL_N;
    } else if (nx == x - 1 && ny == y) {
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

    const int dx[4] = {0, 1, 0, -1};
    const int dy[4] = {-1, 0, 1, 0};

    while (top > 0) {
        P c = stack[top - 1];
        int n = 0;
        P opts[4];

        for (int i = 0; i < 4; i++) {
            int nx = c.x + dx[i], ny = c.y + dy[i];
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

    for (int y = 0; y < MAZE_H; y++)
        for (int x = 0; x < MAZE_W; x++)
            g[y][x].visited = false;
}

static bool try_move(int* px, int* py, int dx, int dy) {
    int x = *px, y = *py;
    int nx = x + dx, ny = y + dy;
    if (!in_bounds(nx, ny)) return false;

    uint8_t w = g[y][x].walls;

    if ((dx == 0 && dy == -1 && (w & WALL_N)) ||
        (dx == 1 && dy == 0  && (w & WALL_E)) ||
        (dx == 0 && dy == 1  && (w & WALL_S)) ||
        (dx == -1 && dy == 0 && (w & WALL_W)))
        return false;

    *px = nx;
    *py = ny;
    return true;
}

static void draw(SDL_Renderer* r, int px, int py) {
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

    SDL_Rect p = {
        PAD + px * CELL + 8,
        PAD + py * CELL + 8,
        CELL - 16, CELL - 16
    };
    SDL_SetRenderDrawColor(r, 255, 215, 0, 255);
    SDL_RenderFillRect(r, &p);

    SDL_RenderPresent(r);
}

static void regenerate(int* px, int* py) {
    if (run_level > 0)
        mongo_finish_level(*px, *py);

    maze_init();
    maze_generate(0, 0);

    *px = 0;
    *py = 0;

    mongo_start_level(*px, *py);
}

int main(void) {
    srand((unsigned)time(NULL));

    mongoc_init();

    client = mongoc_client_new(
        "mongodb://team2f:team2psu@10.170.8.109:8448/?authSource=admin"
    );

    if (!client) {
        fprintf(stderr, "Failed to connect to MongoDB\n");
        return 1;
    }

    collection = mongoc_client_get_collection(
        client,
        "maze_game",
        "runs"
    );

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed\n");
        return 1;
    }

    SDL_Window* win = SDL_CreateWindow(
        "SDL2 Maze Mongo",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        PAD * 2 + MAZE_W * CELL,
        PAD * 2 + MAZE_H * CELL,
        SDL_WINDOW_SHOWN
    );

    SDL_Renderer* r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);

    int px = 0, py = 0;
    regenerate(&px, &py);

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                running = false;

            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;

                if (k == SDLK_ESCAPE)
                    running = false;

                if (k == SDLK_r)
                    regenerate(&px, &py);

                bool moved = false;
                if (k == SDLK_UP) moved = try_move(&px,&py,0,-1);
                else if (k == SDLK_RIGHT) moved = try_move(&px,&py,1,0);
                else if (k == SDLK_DOWN) moved = try_move(&px,&py,0,1);
                else if (k == SDLK_LEFT) moved = try_move(&px,&py,-1,0);

                if (moved)
                    mongo_append_move(px, py);
            }
        }

        draw(r, px, py);
    }

    mongo_finish_level(px, py);

    mongoc_collection_destroy(collection);
    mongoc_client_destroy(client);
    mongoc_cleanup();

    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();

    return 0;
}

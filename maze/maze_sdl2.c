// maze_sdl2.c
// SDL2 Maze with movement logging to JSON files (one file per run + per new level)
// Controls: Arrow keys / WASD
// R = regenerate (creates a NEW JSON file)
// Esc = quit
//
// Output folder (WSL/Windows path):
//   /mnt/c/Users/pavlo/abcapsp26FriT2/maze/json_logs
//
// JSON requirements implemented:
// - created_at timestamp
// - moves: array of {x,y} coordinates (movement history, includes starting position)
// - current: {x,y} written at the end when file closes (on R or exit)
// - new file per program run AND per new level

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define MAZE_W 21
#define MAZE_H 15
#define CELL   32
#define PAD    16

#define JSON_DIR "/mnt/c/Users/pavlo/abcapsp26FriT2/maze/json_logs"

enum { WALL_N = 1, WALL_E = 2, WALL_S = 4, WALL_W = 8 };

typedef struct {
  uint8_t walls;
  bool visited;
} Cell;

static Cell g[MAZE_H][MAZE_W];

// ---------------- JSON logging ----------------
static FILE* json_file = NULL;
static bool first_move = true;

static char run_stamp[32] = {0};  // e.g. 20260123_213455
static int level_index = 0;       // 1,2,3... each regenerate creates a new file

static void ensure_json_dir(void) {
  struct stat st;
  if (stat(JSON_DIR, &st) == -1) {
    if (mkdir(JSON_DIR, 0755) != 0 && errno != EEXIST) {
      perror("mkdir JSON_DIR");
    }
  }
}

static void make_run_stamp_once(void) {
  if (run_stamp[0] != '\0') return;

  time_t now = time(NULL);
  struct tm tmv;
  localtime_r(&now, &tmv);

  strftime(run_stamp, sizeof(run_stamp), "%Y%m%d_%H%M%S", &tmv);
}

// ISO8601-ish local time (no offset)
static void make_created_at(char out[32]) {
  time_t now = time(NULL);
  struct tm tmv;
  localtime_r(&now, &tmv);
  strftime(out, 32, "%Y-%m-%dT%H:%M:%S", &tmv);
}

static void json_close_with_current(int px, int py) {
  if (!json_file) return;

  // Finish moves array and write current coordinate at the end (requirement)
  fprintf(json_file, "\n  ],\n");
  fprintf(json_file, "  \"current\": { \"x\": %d, \"y\": %d }\n", px, py);
  fprintf(json_file, "}\n");

  fclose(json_file);
  json_file = NULL;
}

static void json_open_new_file(int start_x, int start_y) {
  make_run_stamp_once();
  level_index++;

  ensure_json_dir();

  char filename[256];
  snprintf(
    filename,
    sizeof(filename),
    "%s/moves_%s_level%02d.json",
    JSON_DIR,
    run_stamp,
    level_index
  );

  json_file = fopen(filename, "w");
  if (!json_file) {
    perror("fopen");
    return;
  }

  char created_at[32];
  make_created_at(created_at);

  // Start JSON and moves list
  fprintf(json_file, "{\n");
  fprintf(json_file, "  \"created_at\": \"%s\",\n", created_at);
  fprintf(json_file, "  \"level\": %d,\n", level_index);
  fprintf(json_file, "  \"moves\": [\n");

  // Log starting coordinate as first move
  fprintf(json_file, "    { \"x\": %d, \"y\": %d }", start_x, start_y);
  first_move = false;

  fflush(json_file);
}

static void json_append_move(int x, int y) {
  if (!json_file) return;

  // Always append with a comma+newline because the first entry is written at open
  fprintf(json_file, ",\n    { \"x\": %d, \"y\": %d }", x, y);
  fflush(json_file);
}
// ------------------------------------------------

static inline bool in_bounds(int x, int y) {
  return (x >= 0 && x < MAZE_W && y >= 0 && y < MAZE_H);
}

static void knock_down(int x, int y, int nx, int ny) {
  if (nx == x && ny == y - 1) {
    g[y][x].walls &= (uint8_t)~WALL_N;
    g[ny][nx].walls &= (uint8_t)~WALL_S;
  } else if (nx == x + 1 && ny == y) {
    g[y][x].walls &= (uint8_t)~WALL_E;
    g[ny][nx].walls &= (uint8_t)~WALL_W;
  } else if (nx == x && ny == y + 1) {
    g[y][x].walls &= (uint8_t)~WALL_S;
    g[ny][nx].walls &= (uint8_t)~WALL_N;
  } else if (nx == x - 1 && ny == y) {
    g[y][x].walls &= (uint8_t)~WALL_W;
    g[ny][nx].walls &= (uint8_t)~WALL_E;
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

  const int dx[4] = { 0, 1, 0, -1 };
  const int dy[4] = { -1, 0, 1, 0 };

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

<<<<<<< HEAD
// Draw maze walls as lines
static void draw_maze(SDL_Renderer* r) {
  // Background
  SDL_SetRenderDrawColor(r, 15, 15, 18, 255);
  SDL_RenderClear(r);

  // Maze lines
  SDL_SetRenderDrawColor(r, 230, 230, 230, 255);

  int ox = PAD;
  int oy = PAD;

  for (int y = 0; y < MAZE_H; y++) {
    for (int x = 0; x < MAZE_W; x++) {
      int x0 = ox + x * CELL;
      int y0 = oy + y * CELL;
      int x1 = x0 + CELL;
      int y1 = y0 + CELL;

      uint8_t w = g[y][x].walls;

      if (w & WALL_N) SDL_RenderDrawLine(r, x0, y0, x1, y0);
      if (w & WALL_E) SDL_RenderDrawLine(r, x1, y0, x1, y1);
      if (w & WALL_S) SDL_RenderDrawLine(r, x0, y1, x1, y1);
      if (w & WALL_W) SDL_RenderDrawLine(r, x0, y0, x0, y1);
    }
  }
}

// Player / goal rendering
static void draw_player_goal(SDL_Renderer* r, int px, int py) {
  int ox = PAD;
  int oy = PAD;

  // Goal cell highlight
  SDL_Rect goal = {
    ox + (MAZE_W - 1) * CELL + 6,
    oy + (MAZE_H - 1) * CELL + 6,
    CELL - 12,
    CELL - 12
  };
  SDL_SetRenderDrawColor(r, 40, 160, 70, 255);
  SDL_RenderFillRect(r, &goal);

  // Player
  SDL_Rect p = {
    ox + px * CELL + 8,
    oy + py * CELL + 8,
    CELL - 16,
    CELL - 16
  };
  SDL_SetRenderDrawColor(r, 255, 215, 0, 255);
  SDL_RenderFillRect(r, &p);
}

// Attempt to move player; returns true if moved
=======
>>>>>>> dc3b009d51850d696213474f7c842b6b75e5e48b
static bool try_move(int* px, int* py, int dx, int dy) {
  int x = *px, y = *py;
  int nx = x + dx, ny = y + dy;
  if (!in_bounds(nx, ny)) return false;

  uint8_t w = g[y][x].walls;

  if ((dx == 0 && dy == -1 && (w & WALL_N)) ||
      (dx == 1 && dy == 0  && (w & WALL_E)) ||
      (dx == 0 && dy == 1  && (w & WALL_S)) ||
      (dx == -1 && dy == 0 && (w & WALL_W))) {
    return false;
  }

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

  // Goal (green)
  SDL_Rect goal = {
    PAD + (MAZE_W - 1) * CELL + 6,
    PAD + (MAZE_H - 1) * CELL + 6,
    CELL - 12, CELL - 12
  };
  SDL_SetRenderDrawColor(r, 40, 160, 70, 255);
  SDL_RenderFillRect(r, &goal);

  // Player (yellow)
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
  // Close the previous file with current coordinate at the end
  if (json_file) {
    json_close_with_current(*px, *py);
  }

  maze_init();
  maze_generate(0, 0);

  *px = 0;
  *py = 0;

  // Open a NEW JSON file for this level and log starting coordinate
  json_open_new_file(*px, *py);
}

int main(void) {
  srand((unsigned)time(NULL));

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Window* win = SDL_CreateWindow(
    "SDL2 Maze",
    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    PAD * 2 + MAZE_W * CELL,
    PAD * 2 + MAZE_H * CELL,
    SDL_WINDOW_SHOWN
  );
  if (!win) {
    fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_Renderer* r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  if (!r) {
    fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
  }

  int px = 0, py = 0;

  // First level: creates a NEW JSON file for this run
  regenerate(&px, &py);

  bool running = true;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        running = false;
        break;
      }

      if (e.type == SDL_KEYDOWN) {
        SDL_Keycode k = e.key.keysym.sym;

        if (k == SDLK_ESCAPE) {
          running = false;
          break;
        }

        if (k == SDLK_r) {
          regenerate(&px, &py);
          break;
        }

        bool moved = false;
        if (k == SDLK_UP || k == SDLK_w) moved = try_move(&px, &py, 0, -1);
        else if (k == SDLK_RIGHT || k == SDLK_d) moved = try_move(&px, &py, 1, 0);
        else if (k == SDLK_DOWN || k == SDLK_s) moved = try_move(&px, &py, 0, 1);
        else if (k == SDLK_LEFT || k == SDLK_a) moved = try_move(&px, &py, -1, 0);

        if (moved) {
          json_append_move(px, py);
        }
      }
    }

    draw(r, px, py);
  }

  // Close the last file, ensuring current is at the end
  if (json_file) {
    json_close_with_current(px, py);
  }

  SDL_DestroyRenderer(r);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}

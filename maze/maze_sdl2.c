// maze_sdl2.c
// SDL2 Maze client that sends JSON moves to HTTPS server (/move) using libcurl
// Controls: Arrow keys / WASD, R regenerate, Esc quit

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <curl/curl.h>

#define MAZE_W 21
#define MAZE_H 15
#define CELL   32
#define PAD    16

// CHANGE THIS IF YOUR SERVER IP/PORT IS DIFFERENT
#define MOVE_URL "https://10.170.8.130:8448/move"
#define MISSION_URL "https://10.170.8.109:8448/mission"

enum { WALL_N = 1, WALL_E = 2, WALL_S = 4, WALL_W = 8 };

typedef struct {
  uint8_t walls;
  bool visited;
} Cell;

static Cell g[MAZE_H][MAZE_W];

// ---------- HTTPS JSON sender (libcurl) ----------
static void iso_utc_now(char out[32]) {
  time_t t = time(NULL);
  struct tm tmv;
  gmtime_r(&t, &tmv);
  strftime(out, 32, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

static int https_post_json(const char *url, const char *json_body) {
  CURL *curl = curl_easy_init();
  if (!curl) return 0;

  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);

  // self-signed cert in lab:
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

  CURLcode res = curl_easy_perform(curl);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  return (res == CURLE_OK);
}

static int move_sequence = 0;
static int level_index = 0;

static void send_event(const char *event_type, int x, int y, bool goal_reached) {
  char ts[32];
  iso_utc_now(ts);

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
    event_type,
    level_index,
    move_sequence,
    x, y,
    goal_reached ? "true" : "false",
    ts
  );

  if (!https_post_json(MOVE_URL, json)) {
    fprintf(stderr, "POST failed: %s\n", event_type);
  }
}
// -----------------------------------------------

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

static void send_mission_payload(int level, int w, int h) {
    char ts[32];
    iso_utc_now(ts);

    char json[512];
    snprintf(json, sizeof(json),
        "{"
          "\"event_type\":\"mission_setup\","
          "\"team_id\":\"Team 2 Friday\","
          "\"mission_id\":\"T2_FRI_LVL_%d\","
          "\"map_dimensions\":{\"width\":%d,\"height\":%d},"
          "\"timestamp\":\"%s\""
        "}",
        level, w, h, ts
    );

    if (!https_post_json(MISSION_URL, json)) {
        fprintf(stderr, "AI Server (10.170.8.109) Mission Payload failed!\n");
    } else {
        printf("Mission Payload sent to AI Server.\n");
    }
}

static void regenerate(int* px, int* py) {
  // send end event for previous level (optional but nice)
  send_event("session_end", *px, *py, (*px == MAZE_W-1 && *py == MAZE_H-1));

  maze_init();
  maze_generate(0, 0);

  *px = 0;
  *py = 0;

  level_index++;
  move_sequence = 0;

  // send start event
  send_mission_payload(level_index, MAZE_W, MAZE_H);
  send_event("level_start", *px, *py, false);
}

int main(void) {
  srand((unsigned)time(NULL));
  curl_global_init(CURL_GLOBAL_DEFAULT);

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

  // First level
  level_index = 0;
  regenerate(&px, &py); // increments to level 1 and sends start event

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
          move_sequence++;
          bool goal = (px == MAZE_W - 1 && py == MAZE_H - 1);
          send_event("player_move", px, py, goal);
        }
      }
    }

    draw(r, px, py);
  }

  // final end event
  send_event("session_end", px, py, (px == MAZE_W-1 && py == MAZE_H-1));

  SDL_DestroyRenderer(r);
  SDL_DestroyWindow(win);
  SDL_Quit();
  curl_global_cleanup();
  return 0;
}

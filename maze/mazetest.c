// maze_terminal.c
// Terminal maze with MongoDB logging + A* solver

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <string.h>

#include <mongoc/mongoc.h>

#define MAZE_W 21
#define MAZE_H 15

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
    if (move_count < MAX_MOVES)
        move_buffer[move_count++] = (Move){x, y};
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
    for (int y=0;y<MAZE_H;y++)
        for (int x=0;x<MAZE_W;x++) {
            g[y][x].walls = WALL_N|WALL_E|WALL_S|WALL_W;
            g[y][x].visited = false;
        }
}

static void maze_generate(int sx,int sy) {

    typedef struct {int x,y;} P;
    P stack[MAZE_W*MAZE_H];
    int top=0;

    g[sy][sx].visited=true;
    stack[top++] = (P){sx,sy};

    int dx[4]={0,1,0,-1};
    int dy[4]={-1,0,1,0};

    while(top>0){

        P c = stack[top-1];
        int n=0;
        P opts[4];

        for(int i=0;i<4;i++){
            int nx=c.x+dx[i];
            int ny=c.y+dy[i];

            if(in_bounds(nx,ny) && !g[ny][nx].visited)
                opts[n++] = (P){nx,ny};
        }

        if(n==0){ top--; continue; }

        P next = opts[rand()%n];

        knock_down(c.x,c.y,next.x,next.y);

        g[next.y][next.x].visited=true;
        stack[top++] = next;
    }

    for(int y=0;y<MAZE_H;y++)
        for(int x=0;x<MAZE_W;x++)
            g[y][x].visited=false;
}

/* ---------------- A* Pathfinding ---------------- */

typedef struct {
    int x,y;
    int g,f;
    int parent;
    bool closed;
} Node;

#define MAX_NODES (MAZE_W*MAZE_H)

static int heuristic(int x,int y,int gx,int gy){
    return abs(x-gx)+abs(y-gy);
}

static int astar_path(Move path[MAX_MOVES]){

    Node nodes[MAX_NODES];
    int open[MAX_NODES];
    int open_count=0;

    for(int i=0;i<MAX_NODES;i++){
        nodes[i].g=999999;
        nodes[i].f=999999;
        nodes[i].parent=-1;
        nodes[i].closed=false;
    }

    int start=0;
    int goal = (MAZE_H-1)*MAZE_W + (MAZE_W-1);

    nodes[start].g=0;
    nodes[start].f=heuristic(0,0,MAZE_W-1,MAZE_H-1);

    open[open_count++]=start;

    while(open_count>0){

        int best=0;
        for(int i=1;i<open_count;i++)
            if(nodes[open[i]].f < nodes[open[best]].f)
                best=i;

        int current=open[best];
        open[best]=open[--open_count];

        if(current==goal) break;

        nodes[current].closed=true;

        int cx=current%MAZE_W;
        int cy=current/MAZE_W;

        int dirs[4][2]={{0,-1},{1,0},{0,1},{-1,0}};

        for(int d=0;d<4;d++){

            int nx=cx+dirs[d][0];
            int ny=cy+dirs[d][1];

            if(!in_bounds(nx,ny)) continue;

            uint8_t w=g[cy][cx].walls;

            if((d==0 && (w&WALL_N)) ||
               (d==1 && (w&WALL_E)) ||
               (d==2 && (w&WALL_S)) ||
               (d==3 && (w&WALL_W)))
                continue;

            int ni = ny*MAZE_W + nx;

            if(nodes[ni].closed) continue;

            int newg = nodes[current].g + 1;

            if(newg < nodes[ni].g){

                nodes[ni].g=newg;
                nodes[ni].f=newg + heuristic(nx,ny,MAZE_W-1,MAZE_H-1);
                nodes[ni].parent=current;

                open[open_count++]=ni;
            }
        }
    }

    int idx=goal;
    int len=0;

    while(idx!=-1){
        path[len++] = (Move){ idx%MAZE_W, idx/MAZE_W };
        idx = nodes[idx].parent;
    }

    for(int i=0;i<len/2;i++){
        Move t=path[i];
        path[i]=path[len-i-1];
        path[len-i-1]=t;
    }

    return len;
}

/* ---------------- Display ---------------- */

static void print_maze(int px,int py){

    for(int y=0;y<MAZE_H;y++){

        for(int x=0;x<MAZE_W;x++){
            printf("+");
            printf((g[y][x].walls & WALL_N)?"---":"   ");
        }
        printf("+\n");

        for(int x=0;x<MAZE_W;x++){

            printf((g[y][x].walls & WALL_W)?"|":" ");

            if(x==px && y==py) printf(" P ");
            else if(x==MAZE_W-1 && y==MAZE_H-1) printf(" G ");
            else printf("   ");
        }

        printf((g[y][MAZE_W-1].walls & WALL_E)?"|\n":" \n");
    }

    for(int x=0;x<MAZE_W;x++) printf("+---");
    printf("+\n");
}

/* ---------------- Game Control ---------------- */

static void regenerate(int*px,int*py){

    if(run_level>0)
        mongo_finish_level(*px,*py);

    maze_init();
    maze_generate(0,0);

    *px=0;
    *py=0;

    mongo_start_level(*px,*py);
}

static void solve_maze(int*px,int*py){

    Move path[MAX_MOVES];
    int len = astar_path(path);

    printf("\nA* Solution (%d steps):\n",len);

    for(int i=1;i<len;i++){

        *px = path[i].x;
        *py = path[i].y;

        mongo_append_move(*px,*py);

        print_maze(*px,*py);
        printf("\n");
    }
}

/* ---------------- Main ---------------- */

int main(){

    srand(time(NULL));

    mongoc_init();

    client = mongoc_client_new(
        "mongodb://team2f:team2psu@10.170.8.109:8448/?authSource=admin"
    );

    if(!client){
        printf("Mongo connection failed\n");
        return 1;
    }

    collection = mongoc_client_get_collection(client,"maze_game","runs");

    int px=0,py=0;

    regenerate(&px,&py);

    char cmd[32];

    while(1){

        print_maze(px,py);

        printf("Move w/a/s/d | p=solve | r=regen | q=quit : ");

        if(!fgets(cmd,sizeof(cmd),stdin)) break;

        if(cmd[0]=='q') break;

        if(cmd[0]=='r'){
            regenerate(&px,&py);
            continue;
        }

        if(cmd[0]=='p'){
            solve_maze(&px,&py);
            regenerate(&px,&py);
            continue;
        }

        bool moved=false;

        if(cmd[0]=='w') moved=try_move(&px,&py,0,-1);
        if(cmd[0]=='s') moved=try_move(&px,&py,0,1);
        if(cmd[0]=='a') moved=try_move(&px,&py,-1,0);
        if(cmd[0]=='d') moved=try_move(&px,&py,1,0);

        if(moved) mongo_append_move(px,py);

        if(px==MAZE_W-1 && py==MAZE_H-1){
            printf("🎉 Goal reached!\n");
            regenerate(&px,&py);
        }
    }

    mongo_finish_level(px,py);

    mongoc_collection_destroy(collection);
    mongoc_client_destroy(client);
    mongoc_cleanup();

    return 0;
}
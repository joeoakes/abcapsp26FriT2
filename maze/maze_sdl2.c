// maze_sdl2.c
// SDL2 Maze client with HTTPS logging + A* auto solver (press P)

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

#define MOVE_URL "https://10.170.8.130:8448/move"
#define MISSION_URL "https://10.170.8.109:8448/mission"

enum { WALL_N = 1, WALL_E = 2, WALL_S = 4, WALL_W = 8 };

typedef struct {
    uint8_t walls;
    bool visited;
} Cell;

static Cell g[MAZE_H][MAZE_W];

typedef struct { int x,y; } Node;

static int level_index=0;
static int move_sequence=0;

/* ---------------- HTTPS ---------------- */

static void iso_utc_now(char out[32]){
    time_t t=time(NULL);
    struct tm tmv;
    gmtime_r(&t,&tmv);
    strftime(out,32,"%Y-%m-%dT%H:%M:%SZ",&tmv);
}

static int https_post_json(const char *url,const char *json){
    CURL *curl=curl_easy_init();
    if(!curl) return 0;

    struct curl_slist *headers=NULL;
    headers=curl_slist_append(headers,"Content-Type: application/json");

    curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
    curl_easy_setopt(curl,CURLOPT_POSTFIELDS,json);

    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,0L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,0L);

    CURLcode res=curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res==CURLE_OK);
}

static void send_event(const char *type,int x,int y,bool goal){
    char ts[32];
    iso_utc_now(ts);

    char json[512];

    snprintf(json,sizeof(json),
    "{"
      "\"event_type\":\"%s\","
      "\"level\":%d,"
      "\"input\":{\"device\":\"keyboard\",\"move_sequence\":%d},"
      "\"player\":{\"position\":{\"x\":%d,\"y\":%d}},"
      "\"goal_reached\":%s,"
      "\"timestamp\":\"%s\""
    "}",
    type,level_index,move_sequence,x,y,goal?"true":"false",ts);

    https_post_json(MOVE_URL,json);
}

/* ---------------- Maze ---------------- */

static inline bool in_bounds(int x,int y){
    return x>=0 && x<MAZE_W && y>=0 && y<MAZE_H;
}

static void knock_down(int x,int y,int nx,int ny){

    if(nx==x && ny==y-1){
        g[y][x].walls &= ~WALL_N;
        g[ny][nx].walls &= ~WALL_S;
    }
    else if(nx==x+1 && ny==y){
        g[y][x].walls &= ~WALL_E;
        g[ny][nx].walls &= ~WALL_W;
    }
    else if(nx==x && ny==y+1){
        g[y][x].walls &= ~WALL_S;
        g[ny][nx].walls &= ~WALL_N;
    }
    else if(nx==x-1 && ny==y){
        g[y][x].walls &= ~WALL_W;
        g[ny][nx].walls &= ~WALL_E;
    }
}

static void maze_init(){

    for(int y=0;y<MAZE_H;y++)
        for(int x=0;x<MAZE_W;x++){
            g[y][x].walls=WALL_N|WALL_E|WALL_S|WALL_W;
            g[y][x].visited=false;
        }
}

static void maze_generate(int sx,int sy){

    typedef struct{int x,y;} P;
    P stack[MAZE_W*MAZE_H];
    int top=0;

    g[sy][sx].visited=true;
    stack[top++] = (P){sx,sy};

    int dx[4]={0,1,0,-1};
    int dy[4]={-1,0,1,0};

    while(top>0){

        P c=stack[top-1];
        int n=0;
        P opts[4];

        for(int i=0;i<4;i++){
            int nx=c.x+dx[i];
            int ny=c.y+dy[i];

            if(in_bounds(nx,ny) && !g[ny][nx].visited)
                opts[n++] = (P){nx,ny};
        }

        if(n==0){
            top--;
            continue;
        }

        P next = opts[rand()%n];

        knock_down(c.x,c.y,next.x,next.y);

        g[next.y][next.x].visited=true;
        stack[top++] = next;
    }

    for(int y=0;y<MAZE_H;y++)
        for(int x=0;x<MAZE_W;x++)
            g[y][x].visited=false;
}

/* ---------------- A* Solver ---------------- */

static int heuristic(int x,int y){
    return abs(x-(MAZE_W-1))+abs(y-(MAZE_H-1));
}

static int astar(Node path[MAZE_W*MAZE_H]){

    typedef struct{
        int g,f,parent;
        bool closed;
    } ANode;

    ANode nodes[MAZE_W*MAZE_H];

    for(int i=0;i<MAZE_W*MAZE_H;i++){
        nodes[i].g=999999;
        nodes[i].f=999999;
        nodes[i].parent=-1;
        nodes[i].closed=false;
    }

    int open[MAZE_W*MAZE_H];
    int open_count=0;

    nodes[0].g=0;
    nodes[0].f=heuristic(0,0);
    open[open_count++]=0;

    int goal=(MAZE_H-1)*MAZE_W+(MAZE_W-1);

    while(open_count>0){

        int best=0;

        for(int i=1;i<open_count;i++)
            if(nodes[open[i]].f < nodes[open[best]].f)
                best=i;

        int current=open[best];
        open[best]=open[--open_count];

        if(current==goal)
            break;

        nodes[current].closed=true;

        int cx=current%MAZE_W;
        int cy=current/MAZE_W;

        int dirs[4][2]={{0,-1},{1,0},{0,1},{-1,0}};

        for(int d=0;d<4;d++){

            int nx=cx+dirs[d][0];
            int ny=cy+dirs[d][1];

            if(!in_bounds(nx,ny))
                continue;

            uint8_t w=g[cy][cx].walls;

            if((d==0 && (w&WALL_N)) ||
               (d==1 && (w&WALL_E)) ||
               (d==2 && (w&WALL_S)) ||
               (d==3 && (w&WALL_W)))
                continue;

            int ni=ny*MAZE_W+nx;

            if(nodes[ni].closed)
                continue;

            int newg=nodes[current].g+1;

            if(newg < nodes[ni].g){

                nodes[ni].g=newg;
                nodes[ni].f=newg + heuristic(nx,ny);
                nodes[ni].parent=current;

                open[open_count++]=ni;
            }
        }
    }

    int idx=goal;
    int len=0;

    while(idx!=-1){
        path[len++] = (Node){idx%MAZE_W,idx/MAZE_W};
        idx=nodes[idx].parent;
    }

    for(int i=0;i<len/2;i++){
        Node t=path[i];
        path[i]=path[len-i-1];
        path[len-i-1]=t;
    }

    return len;
}

/* ---------------- Movement ---------------- */

static bool try_move(int *px,int *py,int dx,int dy){

    int x=*px,y=*py;
    int nx=x+dx,ny=y+dy;

    if(!in_bounds(nx,ny)) return false;

    uint8_t w=g[y][x].walls;

    if((dx==0 && dy==-1 && (w&WALL_N)) ||
       (dx==1 && dy==0 && (w&WALL_E)) ||
       (dx==0 && dy==1 && (w&WALL_S)) ||
       (dx==-1 && dy==0 && (w&WALL_W)))
        return false;

    *px=nx;
    *py=ny;

    return true;
}

/* ---------------- Draw ---------------- */

static void draw(SDL_Renderer *r,int px,int py){

    SDL_SetRenderDrawColor(r,15,15,18,255);
    SDL_RenderClear(r);

    SDL_SetRenderDrawColor(r,230,230,230,255);

    for(int y=0;y<MAZE_H;y++)
        for(int x=0;x<MAZE_W;x++){

            int ox=PAD+x*CELL;
            int oy=PAD+y*CELL;
            int ex=ox+CELL;
            int ey=oy+CELL;

            uint8_t w=g[y][x].walls;

            if(w&WALL_N) SDL_RenderDrawLine(r,ox,oy,ex,oy);
            if(w&WALL_E) SDL_RenderDrawLine(r,ex,oy,ex,ey);
            if(w&WALL_S) SDL_RenderDrawLine(r,ox,ey,ex,ey);
            if(w&WALL_W) SDL_RenderDrawLine(r,ox,oy,ox,ey);
        }

    SDL_Rect goal={
        PAD+(MAZE_W-1)*CELL+6,
        PAD+(MAZE_H-1)*CELL+6,
        CELL-12,CELL-12
    };

    SDL_SetRenderDrawColor(r,40,160,70,255);
    SDL_RenderFillRect(r,&goal);

    SDL_Rect p={
        PAD+px*CELL+8,
        PAD+py*CELL+8,
        CELL-16,CELL-16
    };

    SDL_SetRenderDrawColor(r,255,215,0,255);
    SDL_RenderFillRect(r,&p);

    SDL_RenderPresent(r);
}

/* ---------------- Main ---------------- */

int main(){

    srand(time(NULL));

    curl_global_init(CURL_GLOBAL_DEFAULT);

    SDL_Init(SDL_INIT_VIDEO);

    SDL_Window *win = SDL_CreateWindow(
        "SDL2 Maze",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
        PAD*2+MAZE_W*CELL,
        PAD*2+MAZE_H*CELL,
        SDL_WINDOW_SHOWN
    );

    SDL_Renderer *r = SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED);

    maze_init();
    maze_generate(0,0);

    int px=0,py=0;

    bool running=true;

    while(running){

        SDL_Event e;

        while(SDL_PollEvent(&e)){

            if(e.type==SDL_QUIT)
                running=false;

            if(e.type==SDL_KEYDOWN){

                SDL_Keycode k=e.key.keysym.sym;

                bool moved=false;

                if(k==SDLK_ESCAPE) running=false;

                if(k==SDLK_r){
                    maze_init();
                    maze_generate(0,0);
                    px=0;py=0;
                }

                if(k==SDLK_p){

                    Node path[MAZE_W*MAZE_H];
                    int len=astar(path);

                    for(int i=1;i<len;i++){

                        px=path[i].x;
                        py=path[i].y;

                        move_sequence++;

                        bool goal=(px==MAZE_W-1 && py==MAZE_H-1);

                        send_event("astar_move",px,py,goal);

                        draw(r,px,py);

                        SDL_Delay(60);
                    }
                }

                if(k==SDLK_UP||k==SDLK_w) moved=try_move(&px,&py,0,-1);
                if(k==SDLK_RIGHT||k==SDLK_d) moved=try_move(&px,&py,1,0);
                if(k==SDLK_DOWN||k==SDLK_s) moved=try_move(&px,&py,0,1);
                if(k==SDLK_LEFT||k==SDLK_a) moved=try_move(&px,&py,-1,0);

                if(moved){

                    move_sequence++;

                    bool goal=(px==MAZE_W-1 && py==MAZE_H-1);

                    send_event("player_move",px,py,goal);
                }
            }
        }

        draw(r,px,py);
    }

    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    curl_global_cleanup();

    return 0;
}
#include "astar.h"
#include <stdlib.h>
#include <stdio.h>

#define MAX_NODES 10000

typedef struct {
    int f;
    int g;
    int h;
    int parent;
    int used;
} AStarNode;

int heuristic(Node a, Node b)
{
    int dx = abs(a.x - b.x);
    int dy = abs(a.y - b.y);
    return dx + dy;
}

int get_index(int x, int y, int width)
{
    return y * width + x;
}

int astar(int width, int height, int *grid, Node start, Node goal, Path *path)
{
    int total = width * height;

    AStarNode nodes[MAX_NODES];

    for(int i=0;i<total;i++)
    {
        nodes[i].f = 0;
        nodes[i].g = 999999;
        nodes[i].h = 0;
        nodes[i].parent = -1;
        nodes[i].used = 0;
    }

    int start_idx = get_index(start.x,start.y,width);
    int goal_idx = get_index(goal.x,goal.y,width);

    nodes[start_idx].g = 0;
    nodes[start_idx].h = heuristic(start,goal);
    nodes[start_idx].f = nodes[start_idx].h;

    int open[MAX_NODES];
    int open_count = 0;

    open[open_count++] = start_idx;

    while(open_count > 0)
    {
        int best = 0;

        for(int i=1;i<open_count;i++)
        {
            if(nodes[open[i]].f < nodes[open[best]].f)
                best = i;
        }

        int current = open[best];

        open[best] = open[--open_count];

        if(current == goal_idx)
            break;

        nodes[current].used = 1;

        int cx = current % width;
        int cy = current / width;

        int dirs[4][2] = {
            {0,-1},
            {1,0},
            {0,1},
            {-1,0}
        };

        for(int d=0; d<4; d++)
        {
            int nx = cx + dirs[d][0];
            int ny = cy + dirs[d][1];

            if(nx<0 || ny<0 || nx>=width || ny>=height)
                continue;

            int ni = get_index(nx,ny,width);

            if(grid[ni] == 1)
                continue;

            if(nodes[ni].used)
                continue;

            int new_g = nodes[current].g + 1;

            if(new_g < nodes[ni].g)
            {
                nodes[ni].g = new_g;

                Node n = {nx,ny};

                nodes[ni].h = heuristic(n,goal);

                nodes[ni].f = nodes[ni].g + nodes[ni].h;

                nodes[ni].parent = current;

                open[open_count++] = ni;
            }
        }
    }

    int idx = goal_idx;
    int length = 0;

    while(idx != start_idx && idx != -1)
    {
        Node n;
        n.x = idx % width;
        n.y = idx / width;

        path->nodes[length++] = n;

        idx = nodes[idx].parent;

        if(length >= MAX_PATH)
            break;
    }

    path->nodes[length++] = start;

    for(int i=0;i<length/2;i++)
    {
        Node tmp = path->nodes[i];
        path->nodes[i] = path->nodes[length-i-1];
        path->nodes[length-i-1] = tmp;
    }

    path->length = length;

    return 1;
}
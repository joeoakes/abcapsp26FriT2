#ifndef ASTAR_H
#define ASTAR_H

#define MAX_PATH 1024

typedef struct {
    int x;
    int y;
} Node;

typedef struct {
    Node nodes[MAX_PATH];
    int length;
} Path;

int astar(
    int width,
    int height,
    int *grid,
    Node start,
    Node goal,
    Path *path
);

#endif
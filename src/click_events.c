// vim:ts=4:sw=4:expandtab
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_tree.h>

#include "i3status.h"

/*
 * Reads the click events i3bar/swaybar writes to stdin (an endless JSON
 * array, one event per line), hands each left click to the module that was
 * clicked, and then wakes the main thread so the result shows right away.
 *
 */
static void *read_click_events(void *unused) {
    char *line = NULL;
    size_t size = 0;
    const char *name_path[] = {"name", NULL};
    const char *button_path[] = {"button", NULL};

    while (getline(&line, &size, stdin) != -1) {
        /* Each event after the array's opening "[" begins with "," */
        const char *event = line + strspn(line, " \t[,");
        yajl_val root = yajl_tree_parse(event, NULL, 0);
        if (root == NULL)
            continue;
        yajl_val name = yajl_tree_get(root, name_path, yajl_t_string);
        yajl_val button = yajl_tree_get(root, button_path, yajl_t_number);
        if (name != NULL && button != NULL && YAJL_GET_INTEGER(button) == 1) {
            if (strcmp(YAJL_GET_STRING(name), "vpn") == 0)
                click_vpn(cfg_getsec(cfg, "vpn"));
            pthread_kill(main_thread, SIGUSR1);
        }
        yajl_tree_free(root);
    }
    free(line);
    return NULL;
}

void start_click_events(void) {
    pthread_t thread;
    if (pthread_create(&thread, NULL, read_click_events, NULL) != 0)
        die("Could not create the click events thread\n");
    pthread_detach(thread);
}

// vim:ts=4:sw=4:expandtab
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>
#include <dirent.h>
#include <unistd.h>
#include <net/if.h>
#include <limits.h>

#include "i3status.h"

#define OTHERS_SIZE 512

/* Whether the network interface exists and is administratively up. */
static bool interface_up(const char *interface) {
    char path[PATH_MAX];
    char flags[32];
    snprintf(path, sizeof(path), "/sys/class/net/%s/flags", interface);
    if (!slurp(path, flags, sizeof(flags)))
        return false;
    return strtoul(flags, NULL, 16) & IFF_UP;
}

/* Whether the network interface is a VPN tunnel: WireGuard, or tun/tap (OpenVPN). */
static bool interface_is_vpn(const char *interface) {
    char path[PATH_MAX];
    char uevent[512];
    snprintf(path, sizeof(path), "/sys/class/net/%s/tun_flags", interface);
    if (access(path, F_OK) == 0)
        return true;
    snprintf(path, sizeof(path), "/sys/class/net/%s/uevent", interface);
    return slurp(path, uevent, sizeof(uevent)) && strstr(uevent, "DEVTYPE=wireguard\n") != NULL;
}

void print_vpn(vpn_ctx_t *ctx) {
    char *outwalk = ctx->buf;

    if (ctx->interface == NULL) {
        OUTPUT_FULL_TEXT("vpn: interface not configured");
        return;
    }

    /* Every other VPN interface that is up, each formatted with format_other. */
    char others[OTHERS_SIZE] = "";
    DIR *dir = opendir("/sys/class/net");
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.' || strcmp(entry->d_name, ctx->interface) == 0)
                continue;
            if (!interface_is_vpn(entry->d_name) || !interface_up(entry->d_name))
                continue;
            placeholder_t placeholders[] = {{.name = "%name", .value = entry->d_name}};
            char *other = format_placeholders(ctx->format_other, &placeholders[0], 1);
            strncat(others, other, sizeof(others) - strlen(others) - 1);
            free(other);
        }
        closedir(dir);
    }

    const bool up = interface_up(ctx->interface);
    INSTANCE(ctx->interface);
    START_COLOR(up ? "color_good" : "color_bad");

    placeholder_t placeholders[] = {
        {.name = "%name", .value = ctx->name ? ctx->name : ctx->interface},
        {.name = "%others", .value = others}};
    const size_t num = sizeof(placeholders) / sizeof(placeholder_t);
    char *formatted = format_placeholders(up ? ctx->format_up : ctx->format_down, &placeholders[0], num);
    OUTPUT_FORMATTED;
    free(formatted);

    END_COLOR;
    OUTPUT_FULL_TEXT(ctx->buf);
}

/* Disconnects the VPN if its interface is up, connects it otherwise. */
void click_vpn(cfg_t *sec) {
    const char *interface = cfg_getstr(sec, "interface");
    if (interface == NULL)
        return;
    const char *command = cfg_getstr(sec, interface_up(interface) ? "disconnect" : "connect");
    if (command != NULL)
        (void)system(command);
}

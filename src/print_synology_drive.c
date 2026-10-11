// vim:ts=4:sw=4:expandtab
//
// Shows whether the Synology Drive client is running, connected to the NAS
// and syncing, and how long ago the local data was last known to be in sync.
//
// The client has no status interface, so the state is pieced together:
// - running:   the daemon's pid file names a live process
// - connected: some socket is ESTABLISHED to the NAS's Drive port (6690)
// - syncing:   the daemon log's last sync activity (events, rescans,
//              transfers) is newer than its last "Syncer event pool is now
//              empty" line
//
// While connected and not syncing, the data is in sync now. The daemon logs
// nothing while it idles, so that moment is remembered in memory; after a
// restart of i3status, the log's last "pool is empty" line stands in for it.

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_version.h>

#include "i3status.h"

#define STRING_SIZE 64
#define CHUNK_SIZE 65536

/* What the daemon log has told so far. */
static ino_t log_inode;
static off_t log_offset;
static time_t last_idle;     /* last "pool is now empty" line */
static time_t last_activity; /* last line showing sync work */
static time_t last_in_sync;  /* last time we saw it connected and idle */

/* Parses the line's leading local timestamp, like 2026-10-11T13:04:09. */
static time_t parse_timestamp(const char *line) {
    struct tm tm = {0};
    if (strptime(line, "%Y-%m-%dT%H:%M:%S", &tm) == NULL)
        return 0;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

static void parse_line(const char *line) {
    static const char *activity[] = {
        "] event-manager.cpp(", /* PushEvent, PullEvent, DoneEvent */
        "] rescan-handler.cpp(",
        "] upload-local-handler.cpp(",
        "] download-remote-handler.cpp(",
        "): sending ",
        "): receiving ",
    };

    if (strstr(line, "Syncer event pool is now empty") != NULL) {
        last_idle = max(last_idle, parse_timestamp(line));
        return;
    }
    for (size_t i = 0; i < sizeof(activity) / sizeof(activity[0]); i++) {
        if (strstr(line, activity[i]) != NULL) {
            last_activity = max(last_activity, parse_timestamp(line));
            return;
        }
    }
}

/* Parses the lines of path from *offset on, up to the last complete one,
 * and advances *offset past it. */
static void read_log(const char *path, off_t *offset) {
    FILE *file = fopen(path, "r");
    if (file == NULL)
        return;
    if (fseeko(file, *offset, SEEK_SET) != 0) {
        fclose(file);
        return;
    }

    char *chunk = scalloc(CHUNK_SIZE + 1);
    size_t kept = 0; /* bytes of an incomplete line carried over */
    size_t n;
    while ((n = fread(chunk + kept, 1, CHUNK_SIZE - kept, file)) > 0) {
        const size_t len = kept + n;
        chunk[len] = '\0';
        char *line = chunk;
        char *newline;
        while ((newline = memchr(line, '\n', chunk + len - line)) != NULL) {
            *newline = '\0';
            parse_line(line);
            line = newline + 1;
        }
        kept = chunk + len - line;
        if (kept == CHUNK_SIZE) {
            /* A line longer than a chunk: skip it. */
            kept = 0;
        }
        memmove(chunk, line, kept);
        *offset += len - kept;
    }
    free(chunk);
    fclose(file);
}

/* Reads whatever the daemon logged since the last call. */
static void update_from_log(const char *dir) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/log/daemon.log", dir);
    struct stat st;
    if (stat(path, &st) != 0)
        return;

    set_timezone(NULL); /* The log uses local time. */
    if (log_inode == 0) {
        /* First call: the newest rotated log may hold the last idle line. */
        char rotated[PATH_MAX];
        off_t rotated_offset = 0;
        snprintf(rotated, sizeof(rotated), "%s/log/daemon.log_0", dir);
        read_log(rotated, &rotated_offset);
    }
    if (st.st_ino != log_inode || st.st_size < log_offset) {
        /* Rotated or truncated: start over in the new file. */
        log_inode = st.st_ino;
        log_offset = 0;
    }
    read_log(path, &log_offset);
}

/* Whether some TCP socket is ESTABLISHED to the given remote port. */
static bool connected_to_port(int port) {
    static const char *tables[] = {"/proc/net/tcp", "/proc/net/tcp6"};
    for (size_t i = 0; i < sizeof(tables) / sizeof(tables[0]); i++) {
        FILE *file = fopen(tables[i], "r");
        if (file == NULL)
            continue;
        char line[512];
        bool found = false;
        /* Each line: "sl local_addr:port rem_addr:port st ...", hex. */
        while (!found && fgets(line, sizeof(line), file) != NULL) {
            unsigned int remote_port, state;
            if (sscanf(line, "%*s %*[0-9A-Fa-f]:%*x %*[0-9A-Fa-f]:%x %x", &remote_port, &state) == 2)
                found = (remote_port == (unsigned int)port && state == 0x01 /* TCP_ESTABLISHED */);
        }
        fclose(file);
        if (found)
            return true;
    }
    return false;
}

static void format_age(char *out, size_t size, time_t since, time_t now) {
    if (since == 0) {
        snprintf(out, size, "?");
        return;
    }
    const long minutes = max(now - since, 0) / 60;
    const long hours = minutes / 60;
    const long days = hours / 24;
    if (days > 0)
        snprintf(out, size, "%ldd%02ldh", days, hours % 24);
    else if (hours > 0)
        snprintf(out, size, "%ldh%02ldm", hours, minutes % 60);
    else if (minutes > 0)
        snprintf(out, size, "%ldm", minutes);
    else
        snprintf(out, size, "<1m");
}

void print_synology_drive(synology_drive_ctx_t *ctx) {
    char *outwalk = ctx->buf;
    const time_t now = time(NULL);

    char *dir = resolve_tilde(ctx->path);
    char pidfile[PATH_MAX];
    snprintf(pidfile, sizeof(pidfile), "%s/daemon.pid", dir);
    update_from_log(dir);
    const bool running = process_runs(pidfile);
    free(dir);

    const bool connected = running && connected_to_port(ctx->port);
    const bool syncing = connected && last_activity > last_idle;
    if (connected && !syncing)
        last_in_sync = now;
    last_in_sync = max(last_in_sync, last_idle);

    const char *format, *colorstr;
    if (!running) {
        format = ctx->format_down;
        colorstr = "color_bad";
    } else if (!connected) {
        format = ctx->format_offline;
        colorstr = "color_degraded";
    } else if (syncing) {
        format = ctx->format_syncing;
        colorstr = NULL;
    } else {
        format = ctx->format_synced;
        colorstr = "color_good";
    }

    if (colorstr != NULL)
        START_COLOR(colorstr);

    char age[STRING_SIZE];
    format_age(age, sizeof(age), last_in_sync, now);
    placeholder_t placeholders[] = {
        {.name = "%age", .value = age}};
    const size_t num = sizeof(placeholders) / sizeof(placeholder_t);
    char *formatted = format_placeholders(format, &placeholders[0], num);
    OUTPUT_FORMATTED;
    free(formatted);

    if (colorstr != NULL)
        END_COLOR;

    OUTPUT_FULL_TEXT(ctx->buf);
}

// vim:ts=4:sw=4:expandtab
//
// Shows how much of the Claude subscription's 5-hour and 7-day usage limits
// is left, and when they reset. Uses the OAuth token that Claude Code stores
// in ~/.claude/.credentials.json. The token is only read, never refreshed:
// refreshing would rotate the refresh token and log Claude Code out.
//
// Fetching happens in a background thread so a slow network never blocks the
// bar; between fetches the last known values are displayed.

#include <curl/curl.h>
#include <locale.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <yajl/yajl_gen.h>
#include <yajl/yajl_tree.h>

#include "i3status.h"

#define STRING_SIZE 64
#define CREDENTIALS_SIZE 65536
#define USAGE_URL "https://api.anthropic.com/api/oauth/usage"

struct usage_window {
    double utilization; /* percent used, < 0 if unknown */
    time_t resets_at;   /* 0 if unknown */
};

/* State shared between the main loop and the fetcher thread. */
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static struct usage_window five_hour = {-1, 0};
static struct usage_window seven_day = {-1, 0};
static time_t last_attempt;
static time_t last_success;
static bool fetching;
static char last_error[STRING_SIZE] = "loading";

struct response {
    char *data;
    size_t len;
};

static size_t write_response(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct response *resp = userdata;
    const size_t n = size * nmemb;
    char *grown = realloc(resp->data, resp->len + n + 1);
    if (grown == NULL)
        return 0;
    memcpy(grown + resp->len, ptr, n);
    resp->data = grown;
    resp->len += n;
    resp->data[resp->len] = '\0';
    return n;
}

/* Parses timestamps like 2026-10-05T00:00:00.240205+00:00 */
static time_t parse_iso8601(const char *str) {
    struct tm tm = {0};
    const char *rest = strptime(str, "%Y-%m-%dT%H:%M:%S", &tm);
    if (rest == NULL)
        return 0;
    if (*rest == '.')
        rest += strspn(rest + 1, "0123456789") + 1;

    time_t t = timegm(&tm);
    int off_hours, off_minutes;
    if ((*rest == '+' || *rest == '-') && sscanf(rest + 1, "%2d:%2d", &off_hours, &off_minutes) == 2) {
        const int offset = off_hours * 3600 + off_minutes * 60;
        t += (*rest == '+' ? -offset : offset);
    }
    return t;
}

/* Returns the access token (to be freed by the caller) or NULL with error set. */
static char *read_access_token(const char *path, char *error, size_t error_size) {
    char *contents = scalloc(CREDENTIALS_SIZE);
    if (!slurp(path, contents, CREDENTIALS_SIZE)) {
        snprintf(error, error_size, "no credentials");
        free(contents);
        return NULL;
    }

    yajl_val root = yajl_tree_parse(contents, NULL, 0);
    free(contents);
    if (root == NULL) {
        snprintf(error, error_size, "bad credentials");
        return NULL;
    }

    const char *token_path[] = {"claudeAiOauth", "accessToken", NULL};
    const char *expires_path[] = {"claudeAiOauth", "expiresAt", NULL};
    yajl_val token = yajl_tree_get(root, token_path, yajl_t_string);
    yajl_val expires = yajl_tree_get(root, expires_path, yajl_t_number);

    char *result = NULL;
    if (token == NULL) {
        snprintf(error, error_size, "not logged in");
    } else if (expires != NULL && YAJL_IS_INTEGER(expires) && YAJL_GET_INTEGER(expires) / 1000 < time(NULL)) {
        /* Claude Code refreshes the token the next time it is used. */
        snprintf(error, error_size, "token expired");
    } else {
        result = sstrdup(YAJL_GET_STRING(token));
    }
    yajl_tree_free(root);
    return result;
}

static struct usage_window parse_window(yajl_val root, const char *name) {
    struct usage_window window = {-1, 0};
    const char *utilization_path[] = {name, "utilization", NULL};
    const char *resets_path[] = {name, "resets_at", NULL};
    yajl_val utilization = yajl_tree_get(root, utilization_path, yajl_t_number);
    yajl_val resets_at = yajl_tree_get(root, resets_path, yajl_t_string);
    if (utilization != NULL && YAJL_IS_DOUBLE(utilization))
        window.utilization = YAJL_GET_DOUBLE(utilization);
    if (resets_at != NULL)
        window.resets_at = parse_iso8601(YAJL_GET_STRING(resets_at));
    return window;
}

static void *fetch_usage(void *arg) {
    char *path = arg;
    char error[STRING_SIZE] = "";
    struct usage_window five = {-1, 0}, seven = {-1, 0};

    /* yajl parses numbers with strtod(), which must not use e.g. a German
     * decimal comma. uselocale() only affects this thread. */
    locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (c_locale != (locale_t)0)
        uselocale(c_locale);

    char *token = read_access_token(path, error, sizeof(error));
    free(path);

    if (token != NULL) {
        struct response resp = {NULL, 0};
        char *auth;
        if (asprintf(&auth, "Authorization: Bearer %s", token) == -1)
            die("asprintf failed\n");
        free(token);

        struct curl_slist *headers = curl_slist_append(NULL, auth);
        headers = curl_slist_append(headers, "anthropic-beta: oauth-2025-04-20");

        CURL *curl = curl_easy_init();
        curl_easy_setopt(curl, CURLOPT_URL, USAGE_URL);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "i3status");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        CURLcode res = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

        if (res != CURLE_OK) {
            snprintf(error, sizeof(error), "%s", curl_easy_strerror(res));
        } else if (status != 200) {
            snprintf(error, sizeof(error), "HTTP %ld", status);
        } else {
            yajl_val root = yajl_tree_parse(resp.data, NULL, 0);
            if (root == NULL) {
                snprintf(error, sizeof(error), "bad response");
            } else {
                five = parse_window(root, "five_hour");
                seven = parse_window(root, "seven_day");
                yajl_tree_free(root);
            }
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        free(auth);
        free(resp.data);
    }

    pthread_mutex_lock(&state_lock);
    if (error[0] == '\0') {
        five_hour = five;
        seven_day = seven;
        last_success = time(NULL);
    }
    snprintf(last_error, sizeof(last_error), "%s", error);
    fetching = false;
    pthread_mutex_unlock(&state_lock);

    if (c_locale != (locale_t)0) {
        uselocale(LC_GLOBAL_LOCALE);
        freelocale(c_locale);
    }
    return NULL;
}

/* Kicks off a background fetch if the last one is older than refresh_interval. */
static void maybe_start_fetch(claude_usage_ctx_t *ctx, time_t now) {
    static bool curl_initialized = false;
    if (!curl_initialized) {
        /* Not thread-safe, so do it here in the main thread. */
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_initialized = true;
    }

    if (fetching || (last_attempt != 0 && now - last_attempt < ctx->refresh_interval))
        return;

    char *path = resolve_tilde(ctx->credentials_path);
    pthread_t thread;
    if (pthread_create(&thread, NULL, fetch_usage, path) != 0) {
        free(path);
        return;
    }
    pthread_detach(thread);
    fetching = true;
    last_attempt = now;
}

static void format_countdown(char *out, size_t size, time_t resets_at, time_t now) {
    if (resets_at == 0) {
        snprintf(out, size, "?");
        return;
    }
    const long minutes = (max(resets_at - now, 0) + 59) / 60;
    const long days = minutes / (24 * 60);
    const long hours = minutes / 60 % 24;
    if (days > 0)
        snprintf(out, size, "%ldd%02ldh", days, hours);
    else if (hours > 0)
        snprintf(out, size, "%ldh%02ldm", hours, minutes % 60);
    else
        snprintf(out, size, "%ldm", minutes);
}

static void format_reset_at(char *out, size_t size, const char *format, time_t resets_at) {
    if (resets_at == 0) {
        snprintf(out, size, "?");
        return;
    }
    struct tm tm;
    set_timezone(NULL); /* Use local time. */
    localtime_r(&resets_at, &tm);
    if (strftime(out, size, format, &tm) == 0)
        out[0] = '\0';
}

struct window_strings {
    char left[STRING_SIZE];
    char used[STRING_SIZE];
    char reset[STRING_SIZE];
    char reset_at[STRING_SIZE];
};

static void format_window(claude_usage_ctx_t *ctx, struct window_strings *s, struct usage_window *window, time_t now) {
    /* Once a window has reset, its usage is known to be zero even without fresh data. */
    if (window->resets_at != 0 && window->resets_at <= now) {
        window->utilization = 0;
        window->resets_at = 0;
    }

    if (window->utilization < 0) {
        snprintf(s->left, STRING_SIZE, "?");
        snprintf(s->used, STRING_SIZE, "?");
    } else {
        snprintf(s->left, STRING_SIZE, "%.0f%s", max(100 - window->utilization, 0), pct_mark);
        snprintf(s->used, STRING_SIZE, "%.0f%s", window->utilization, pct_mark);
    }
    format_countdown(s->reset, STRING_SIZE, window->resets_at, now);
    format_reset_at(s->reset_at, STRING_SIZE, ctx->format_reset_at, window->resets_at);
}

void print_claude_usage(claude_usage_ctx_t *ctx) {
    char *outwalk = ctx->buf;
    const time_t now = time(NULL);

    pthread_mutex_lock(&state_lock);
    maybe_start_fetch(ctx, now);
    struct usage_window five = five_hour, seven = seven_day;
    const time_t success = last_success;
    char error[STRING_SIZE];
    snprintf(error, sizeof(error), "%s", last_error);
    pthread_mutex_unlock(&state_lock);

    if (success == 0) {
        placeholder_t placeholders[] = {
            {.name = "%error", .value = error}};
        const size_t num = sizeof(placeholders) / sizeof(placeholder_t);
        char *formatted = format_placeholders(ctx->format_down, &placeholders[0], num);
        OUTPUT_FORMATTED;
        free(formatted);
        OUTPUT_FULL_TEXT(ctx->buf);
        return;
    }

    struct window_strings five_strings, seven_strings;
    format_window(ctx, &five_strings, &five, now);
    format_window(ctx, &seven_strings, &seven, now);

    /* Color by whichever window has the least left. */
    double min_left = 100;
    if (five.utilization >= 0)
        min_left = 100 - five.utilization;
    if (seven.utilization >= 0 && 100 - seven.utilization < min_left)
        min_left = 100 - seven.utilization;

    bool colorful_output = false;
    if (min_left < ctx->threshold_bad) {
        START_COLOR("color_bad");
        colorful_output = true;
    } else if (min_left < ctx->threshold_degraded) {
        START_COLOR("color_degraded");
        colorful_output = true;
    }

    const bool stale = (now - success > 3 * ctx->refresh_interval);

    /* Longer names first: matching picks the first placeholder that is a prefix. */
    placeholder_t placeholders[] = {
        {.name = "%5h_left", .value = five_strings.left},
        {.name = "%5h_used", .value = five_strings.used},
        {.name = "%5h_reset_at", .value = five_strings.reset_at},
        {.name = "%5h_reset", .value = five_strings.reset},
        {.name = "%7d_left", .value = seven_strings.left},
        {.name = "%7d_used", .value = seven_strings.used},
        {.name = "%7d_reset_at", .value = seven_strings.reset_at},
        {.name = "%7d_reset", .value = seven_strings.reset},
        {.name = "%stale", .value = stale ? ctx->stale_marker : ""},
        {.name = "%error", .value = error}};

    const size_t num = sizeof(placeholders) / sizeof(placeholder_t);
    char *untrimmed = format_placeholders(ctx->format, &placeholders[0], num);
    char *formatted = trim(untrimmed);
    OUTPUT_FORMATTED;
    free(formatted);
    free(untrimmed);

    if (colorful_output) {
        END_COLOR;
    }

    OUTPUT_FULL_TEXT(ctx->buf);
}

/*
 * pi_temp_pub.c - Zero-dependency native Raspberry Pi CPU Temperature NATS Publisher
 *
 * Reads CPU temperature from /sys/class/thermal/thermal_zone0/temp,
 * device hostname, and Raspberry Pi hardware model, then publishes an
 * X-Sense-compatible JSON payload directly to a NATS broker over TCP.
 *
 * Configuration:
 *   Automatically loads "pi-temp-pub.conf" from the same folder as the binary
 *   (or a custom path passed via: ./pi-temp-pub /path/to/custom.conf).
 *   If no config file exists, defaults to:
 *     nats_host  = 127.0.0.1
 *     nats_port  = 4222
 *     subject    = sensors.xsense.<hostname>_cpu.telemetry
 *     house_name = White Hatch
 *
 * Compile on Raspberry Pi:
 *   gcc -O2 -Wall -s -o /home/pi/pi-temp-pub/pi-temp-pub /home/pi/pi-temp-pub/pi_temp_pub.c
 */

#include <ctype.h>
#include <libgen.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define THERMAL_PATH "/sys/class/thermal/thermal_zone0/temp"
#define MODEL_PATH   "/proc/device-tree/model"

typedef struct {
    char nats_host[128];
    char nats_port[16];
    char subject[128];
    char house_name[64];
    char room[64];
    char display_name[64];
} Config;

static char *trim_whitespace(char *str) {
    while (*str && isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    /* Strip optional surrounding quotes */
    size_t len = strlen(str);
    if (len >= 2 && ((str[0] == '"' && str[len - 1] == '"') ||
                     (str[0] == '\'' && str[len - 1] == '\''))) {
        str[len - 1] = '\0';
        str++;
    }
    return str;
}

/* Replace {hostname} placeholder in subject if present */
static void expand_hostname_placeholder(char *dest, size_t dest_size, const char *src, const char *hostname) {
    const char *placeholder = "{hostname}";
    const char *pos = strstr(src, placeholder);
    if (!pos) {
        snprintf(dest, dest_size, "%s", src);
        return;
    }
    size_t prefix_len = (size_t)(pos - src);
    if (prefix_len >= dest_size) prefix_len = dest_size - 1;
    char tmp[128];
    memcpy(tmp, src, prefix_len);
    tmp[prefix_len] = '\0';
    snprintf(dest, dest_size, "%s%s%s", tmp, hostname, pos + strlen(placeholder));
}

static void get_default_config_path(char *buf, size_t size) {
    char exe_path[512];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        exe_path[len] = '\0';
        char *dir = dirname(exe_path);
        snprintf(buf, size, "%s/pi-temp-pub.conf", dir);
    } else {
        snprintf(buf, size, "pi-temp-pub.conf");
    }
}

static void load_config(Config *cfg, const char *config_path, const char *hostname, const char *display_host) {
    /* Built-in defaults */
    snprintf(cfg->nats_host, sizeof(cfg->nats_host), "127.0.0.1");
    snprintf(cfg->nats_port, sizeof(cfg->nats_port), "4222");
    snprintf(cfg->subject, sizeof(cfg->subject), "sensors.xsense.%s_cpu.telemetry", hostname);
    snprintf(cfg->house_name, sizeof(cfg->house_name), "White Hatch");
    snprintf(cfg->room, sizeof(cfg->room), "%s_cpu", hostname);
    snprintf(cfg->display_name, sizeof(cfg->display_name), "%s CPU", display_host);

    FILE *fp = fopen(config_path, "r");
    if (!fp) {
        /* Config file is optional; silently use defaults if not present */
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *trimmed = trim_whitespace(line);
        if (*trimmed == '\0' || *trimmed == '#' || *trimmed == ';') {
            continue;
        }
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = trim_whitespace(trimmed);
        char *val = trim_whitespace(eq + 1);

        /* Strip inline comments if unquoted */
        char *hash = strchr(val, '#');
        if (hash) {
            *hash = '\0';
            val = trim_whitespace(val);
        }

        if (*val == '\0') continue;

        if (strcmp(key, "nats_host") == 0) {
            snprintf(cfg->nats_host, sizeof(cfg->nats_host), "%s", val);
        } else if (strcmp(key, "nats_port") == 0) {
            snprintf(cfg->nats_port, sizeof(cfg->nats_port), "%s", val);
        } else if (strcmp(key, "subject") == 0) {
            expand_hostname_placeholder(cfg->subject, sizeof(cfg->subject), val, hostname);
        } else if (strcmp(key, "house_name") == 0) {
            snprintf(cfg->house_name, sizeof(cfg->house_name), "%s", val);
        } else if (strcmp(key, "room") == 0) {
            expand_hostname_placeholder(cfg->room, sizeof(cfg->room), val, hostname);
        } else if (strcmp(key, "display_name") == 0) {
            snprintf(cfg->display_name, sizeof(cfg->display_name), "%s", val);
        }
    }
    fclose(fp);
}

static double read_cpu_temp_c(void) {
    FILE *fp = fopen(THERMAL_PATH, "r");
    if (!fp) {
        perror("Failed to open " THERMAL_PATH);
        exit(1);
    }
    long millideg = 0;
    if (fscanf(fp, "%ld", &millideg) != 1) {
        fclose(fp);
        fprintf(stderr, "Failed to read temperature from %s\n", THERMAL_PATH);
        exit(1);
    }
    fclose(fp);
    return (double)millideg / 1000.0;
}

static void read_pi_model(char *buf, size_t size) {
    FILE *fp = fopen(MODEL_PATH, "r");
    if (!fp) {
        snprintf(buf, size, "Raspberry Pi");
        return;
    }
    size_t n = fread(buf, 1, size - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\0' || buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[--n] = '\0';
    }
    if (buf[0] == '\0') {
        snprintf(buf, size, "Raspberry Pi");
    }
}

static int connect_tcp(const char *host, const char *port) {
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "getaddrinfo(%s:%s): %s\n", host, port, gai_strerror(err));
        return -1;
    }

    int sock = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) continue;

        struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    return sock;
}

int main(int argc, char *argv[]) {
    char hostname[64] = {0};
    if (gethostname(hostname, sizeof(hostname) - 1) != 0) {
        snprintf(hostname, sizeof(hostname), "pi");
    }
    for (size_t i = 0; hostname[i]; i++) {
        hostname[i] = (char)tolower((unsigned char)hostname[i]);
    }

    char display_host[64] = {0};
    for (size_t i = 0; hostname[i] && i < sizeof(display_host) - 1; i++) {
        display_host[i] = (char)toupper((unsigned char)hostname[i]);
    }

    char config_path[512];
    if (argc > 1) {
        snprintf(config_path, sizeof(config_path), "%s", argv[1]);
    } else {
        get_default_config_path(config_path, sizeof(config_path));
    }

    Config cfg;
    load_config(&cfg, config_path, hostname, display_host);

    double temp_c = read_cpu_temp_c();
    double temp_f = temp_c * 9.0 / 5.0 + 32.0;

    char model[128];
    read_pi_model(model, sizeof(model));

    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm utc_tm;
    gmtime_r(&tv.tv_sec, &utc_tm);
    char timestamp_utc[64];
    snprintf(timestamp_utc, sizeof(timestamp_utc),
             "%04d-%02d-%02dT%02d:%02d:%02d.%06ld+00:00",
             utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday,
             utc_tm.tm_hour, utc_tm.tm_min, utc_tm.tm_sec, (long)tv.tv_usec);

    char payload[1024];
    int payload_len = snprintf(payload, sizeof(payload),
        "{"
        "\"timestamp_utc\":\"%s\","
        "\"device_id\":\"%s\","
        "\"room\":\"%s\","
        "\"display_name\":\"%s\","
        "\"station_id\":\"%s\","
        "\"house_name\":\"%s\","
        "\"model\":\"%s\","
        "\"temperature_c\":%.2f,"
        "\"temperature_f\":%.2f,"
        "\"online\":true"
        "}",
        timestamp_utc, cfg.room, cfg.room, cfg.display_name, hostname,
        cfg.house_name, model, temp_c, temp_f);

    printf("Config  : %s\n", config_path);
    printf("Server  : nats://%s:%s\n", cfg.nats_host, cfg.nats_port);
    printf("Subject : %s\n", cfg.subject);
    printf("Payload : %s\n", payload);

    int sock = connect_tcp(cfg.nats_host, cfg.nats_port);
    if (sock < 0) {
        fprintf(stderr, "Error: Could not connect to NATS at %s:%s\n", cfg.nats_host, cfg.nats_port);
        return 1;
    }

    char rx_buf[1024];
    ssize_t r = recv(sock, rx_buf, sizeof(rx_buf) - 1, 0);
    if (r <= 0) {
        fprintf(stderr, "Error: Did not receive NATS INFO banner\n");
        close(sock);
        return 1;
    }

    char tx_buf[1536];
    int tx_len = snprintf(tx_buf, sizeof(tx_buf),
        "CONNECT {\"verbose\":false,\"pedantic\":false}\r\n"
        "PUB %s %d\r\n%s\r\n"
        "PING\r\n",
        cfg.subject, payload_len, payload);

    if (send(sock, tx_buf, (size_t)tx_len, 0) != tx_len) {
        perror("Failed to send NATS PUB command");
        close(sock);
        return 1;
    }

    r = recv(sock, rx_buf, sizeof(rx_buf) - 1, 0);
    close(sock);

    if (r > 0) {
        rx_buf[r] = '\0';
        if (strstr(rx_buf, "-ERR") != NULL) {
            fprintf(stderr, "NATS Broker Error: %s\n", rx_buf);
            return 1;
        }
    }

    printf("Published successfully to nats://%s:%s on '%s'.\n", cfg.nats_host, cfg.nats_port, cfg.subject);
    return 0;
}

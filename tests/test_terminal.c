#include <stdio.h>
#include <unistd.h>
#include "../src/neo.h"

void test_Terminal_EnableRawMode(void) {
    Terminal term = {0};
    int result = Terminal_EnableRawMode(&term);
    if (result == 0 && term.rawModeEnabled) {
        printf("test_Terminal_EnableRawMode: PASS\n\r");
    } else {
        printf("test_Terminal_EnableRawMode: FAIL\n\r");
    }
    Terminal_Restore(&term);
    sleep(1);
}

void test_Terminal_DisableRawMode(void) {
    Terminal term = {0};
    Terminal_EnableRawMode(&term);
    int result = Terminal_DisableRawMode(&term);
    if (result == 0 && !term.rawModeEnabled) {
        printf("test_Terminal_DisableRawMode: PASS\n\r");
    } else {
        printf("test_Terminal_DisableRawMode: FAIL\n\r");
    }
    sleep(1);
}

void test_Terminal_Restore(void) {
    Terminal term = {0};
    Terminal_EnableRawMode(&term);
    int result = Terminal_Restore(&term);
    if (result == 0 && !term.rawModeEnabled) {
        printf("test_Terminal_Restore: PASS\n\r");
    } else {
        printf("test_Terminal_Restore: FAIL\n\r");
    }
    sleep(1);
}

void test_Terminal_ClearScreen(void) {
    Terminal term = {0};
    printf("test_Terminal_ClearScreen: (screen should clear now)\n\r");
    Terminal_ClearScreen(&term);
    printf("test_Terminal_ClearScreen: PASS (if screen cleared)\n\r");
    sleep(1);
} 
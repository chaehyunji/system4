#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define GPIO_TX_DATA "/dev/gpio26"
#define GPIO_TX_CLK  "/dev/gpio27"
#define BIT_DELAY_US 100000
#define LOCK_DURATION 30
#define MAX_FAIL 5

void send_bit(int fd_data, int fd_clk, int bit) {
    const char *val = bit ? "1" : "0";
    write(fd_data, val, 1);
    usleep(5000);
    write(fd_clk, "1", 1);
    usleep(BIT_DELAY_US - 5000);
    write(fd_clk, "0", 1);
}

void send_start_sequence(int fd_data, int fd_clk) {
    unsigned char start_byte = 0xAA; // 10101010
    for (int i = 7; i >= 0; --i) {
        int bit = (start_byte >> i) & 1;
        send_bit(fd_data, fd_clk, bit);
    }
    printf("[TX] Sent start sequence (0xAA)\n");
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0); 
    int fd_data = open(GPIO_TX_DATA, O_WRONLY);
    int fd_clk = open(GPIO_TX_CLK, O_WRONLY);
    if (fd_data < 0 || fd_clk < 0) {
        perror("open");
        return 1;
    }

    int fail_count = 0;
    time_t lock_until = 0;

    while (1) {
        time_t now = time(NULL);
        if (now < lock_until) {
            printf("[LOCKED] Please wait %ld seconds.\n", lock_until - now);
            sleep(1);
            continue;
        }

        setvbuf(stdout, NULL, _IONBF, 0);  

        int c;
        while ((c = getchar()) != '\n' && c != EOF);

        char input[32];
        printf("Enter a 4-digit password (or 'exit' to quit): ");
        fflush(stdout);  

        if (!fgets(input, sizeof(input), stdin)) continue;
        input[strcspn(input, "\n")] = '\0'; 

        if (strcmp(input, "exit") == 0) break;

        char to_send[5];
        if (strlen(input) != 4 || strspn(input, "0123456789") != 4) {
            printf("[WARNING] Invalid input. Sending '0000' as dummy.\n");
            strcpy(to_send, "0000");
            fail_count++;
        } else {
            strcpy(to_send, input);
        }

        send_start_sequence(fd_data, fd_clk);

        for (int i = 0; i < 4; ++i) {
            unsigned char ch = to_send[i];
            for (int b = 7; b >= 0; --b) {
                int bit = (ch >> b) & 1;
                send_bit(fd_data, fd_clk, bit);
                printf("TX bit: %d\n", bit);
            }
        }

        if (strcmp(to_send, "1234") == 0) {
            printf("[CORRECT] Password sent. Exiting.\n");
            break;
        } else if (strcmp(to_send, "0000") != 0) {
            fail_count++;
        }

        if (fail_count >= MAX_FAIL) {
            lock_until = time(NULL) + LOCK_DURATION;
            printf("[LOCKED] %d failed attempts. Wait %d seconds.\n", fail_count, LOCK_DURATION);
        }
    }

    close(fd_data);
    close(fd_clk);
    return 0;
}

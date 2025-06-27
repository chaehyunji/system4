#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>

#define GPIO_RX_DATA "/dev/gpio17"
#define GPIO_RX_CLK  "/dev/gpio19"
#define GPIO_IOCTL_MAGIC       'G'
#define GPIO_IOCTL_ENABLE_IRQ  _IOW(GPIO_IOCTL_MAGIC, 1, int)

#define MAX_FAIL 5
#define LOCK_TIME_SEC 30
#define PASSWORD_LEN 4
#define MAX_BITS (PASSWORD_LEN * 8)

int fd_data, fd_clk;
unsigned char bits[MAX_BITS];
int bit_index = 0;
int fail_count = 0;
time_t lock_until = 0;
int unlocked = 0;
int synced = 0;
unsigned char sync_buf[8] = {0};

char correct_pw[] = "1234";

char bits_to_char(unsigned char *bits) {
    char c = 0;
    for (int i = 0; i < 8; ++i)
        c = (c << 1) | (bits[i] & 1);
    return c;
}

void check_password() {
    char recv[16] = {0};
    for (int i = 0; i < MAX_BITS; i += 8)
        recv[i / 8] = bits_to_char(&bits[i]);
    recv[PASSWORD_LEN] = '\0';

    printf("Received PW: %s\n", recv);

    if (strcmp(recv, correct_pw) == 0) {
        printf("[UNLOCKED]\n");
        unlocked = 1;
        fail_count = 0;
    } else {
        fail_count++;
        if (fail_count >= MAX_FAIL) {
            lock_until = time(NULL) + LOCK_TIME_SEC;
            printf("[LOCKED] %d failed attempts. Wait %d sec.\n", fail_count, LOCK_TIME_SEC);
        } else {
            printf("[DENIED] Failed attempts: %d\n", fail_count);
        }
    }

    bit_index = 0;
    memset(bits, 0, sizeof(bits));
    synced = 0;
    memset(sync_buf, 0, sizeof(sync_buf));
}

void sigio_handler(int signo) {
    time_t now = time(NULL);
    if (now < lock_until || unlocked) {
        bit_index = 0;
        memset(bits, 0, sizeof(bits));
        synced = 0;
        memset(sync_buf, 0, sizeof(sync_buf));
        return;
    }

    char buf[2] = {0};
    lseek(fd_data, 0, SEEK_SET);
    if (read(fd_data, buf, 1) <= 0) return;

    int bit = (buf[0] == '1') ? 1 : 0;

    if (!synced) {
        // shift into sync_buf
        for (int i = 0; i < 7; ++i)
            sync_buf[i] = sync_buf[i+1];
        sync_buf[7] = bit;

        unsigned char pattern = 0;
        for (int i = 0; i < 8; ++i)
            pattern = (pattern << 1) | sync_buf[i];

        if (pattern == 0xAA) {
            synced = 1;
            bit_index = 0;
            memset(bits, 0, sizeof(bits));
            printf("[SYNC] Start pattern detected. Receiving password...\n");
        }
        return;
    }

    if (synced) {
        bits[bit_index++] = bit;
        printf("RX bit %d: %d\n", bit_index - 1, bit);

        if (bit_index >= MAX_BITS) {
            check_password();
        }
    }
}

int main() {
    fd_data = open(GPIO_RX_DATA, O_RDONLY);
    fd_clk = open(GPIO_RX_CLK, O_RDONLY | O_NONBLOCK);
    if (fd_data < 0 || fd_clk < 0) {
        perror("open");
        return 1;
    }

    signal(SIGIO, sigio_handler);
    fcntl(fd_clk, F_SETOWN, getpid());
    fcntl(fd_clk, F_SETFL, O_ASYNC | O_NONBLOCK);
    ioctl(fd_clk, GPIO_IOCTL_ENABLE_IRQ, 0);

    printf("Waiting for password...\n");
    while (!unlocked) {
        if (lock_until > 0 && time(NULL) < lock_until) {
            printf("[LOCKED] Please wait %ld seconds...\n", lock_until - time(NULL));
            sleep(1);
        } else {
            pause();
        }
    }

    close(fd_data);
    close(fd_clk);
    return 0;
}

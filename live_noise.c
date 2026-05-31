#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

int main() {
    // 根據你的 vcamfb 資訊：640x480, 24-bit (每像素 3 bytes)
    int width = 640;
    int height = 480;
    size_t total_bytes = width * height * 3; 

    srand(time(NULL));
    unsigned char *buffer = malloc(total_bytes);
    if (!buffer) return 1;

    // 直接開啟裝置檔案
    int fd = open("/dev/fb1", O_RDWR);
    if (fd < 0) {
        perror("無法開啟 /dev/fb1，請確認是否有 sudo 權限");
        return 1;
    }

    printf("正在持續寫入雜訊至 /dev/fb1... (按下 Ctrl+C 停止)\n");

    while (1) {
        // 產生隨機 RGB 資料
        for (size_t i = 0; i < total_bytes; i++) {
            buffer[i] = rand() % 256;
        }

        // 將指標移回開頭並寫入整幀畫面
        lseek(fd, 0, SEEK_SET);
        if (write(fd, buffer, total_bytes) < 0) {
            perror("寫入失敗");
            break;
        }

        // 稍微暫停，模擬約 30 FPS，避免 CPU 負載過高
        usleep(33000); 
    }

    close(fd);
    free(buffer);
    return 0;
}

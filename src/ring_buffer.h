#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#define RB_SIZE 14400 // 300ms (48000Hz)

class RingBuffer {
private:
    int read_index = 0;
    int write_index = 0;
    int16_t buff[RB_SIZE];

public:
    RingBuffer() {
        write_index = 0;
        read_index = RB_SIZE / 2;

        memset(buff, 0, sizeof(int16_t) * RB_SIZE);
    }

    void init() {
        write_index = 0;
        read_index = RB_SIZE / 2;

        memset(buff, 0, sizeof(int16_t) * RB_SIZE);
    }

    void SetInterval(int interval) {
        interval = interval % RB_SIZE;
        if(interval <= 0) {
            interval = 1;
        }
        write_index = (read_index + interval) % RB_SIZE;
    }

    int16_t Read(int index = 0) {
        int tmp = read_index + index;
        while(tmp < 0) {
            tmp += RB_SIZE;
        }
        tmp = tmp % RB_SIZE;

        return buff[tmp];
    }

    void Write(int16_t in) {
        buff[write_index] = in;
    }

    void Update() {
        read_index = (read_index + 1) % RB_SIZE;
        write_index = (write_index + 1) % RB_SIZE;
    }
};

#endif // RINGBUFFER_H
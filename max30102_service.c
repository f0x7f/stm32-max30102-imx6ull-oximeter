/* max30102_service.c
 * 用法: ./max30102_service /dev/ttySx [/tmp/max30102.sock]
 * 发给GUI的行协议:
 *   S <ir> <red>\n   每收到一个原始样本就推一次(用于画波形, ~100行/秒)
 *   R <hr> <hrv> <spo2> <spv>\n  每算完一窗推一次(~1次/秒)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "algorithm.h"

/* ---------- 帧协议(与STM32 main.c一致) ---------- */
#define HEAD1 0xAA
#define HEAD2 0x55
#define TYPE_HR 0x01
#define PAYLOAD_LEN 8
#define STEP FS /* 每收到100个新样本算一次 */

static uint32_t ir_raw[BUFFER_SIZE], red_raw[BUFFER_SIZE];
static int nbuf = 0; /* 窗内已有样本数 */
static int since_run = 0; /* 距上次计算新到的样本数 */

static int sfd;
static int lsock;
static int clients[8], nclient = 0;
/* 在文件顶部，static 变量区 */
static int32_t hr_history[5];
static int hr_cnt = 0;

/* ---------- 串口打开(115200, 8N1, 原始模式) ---------- */
static int open_serial(const char *dev)
{
	int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		perror("open serial");
		exit(1);
	}
	struct termios tio;
	tcgetattr(fd, &tio);
	cfmakeraw(&tio);
	cfsetispeed(&tio, B115200);
	cfsetospeed(&tio, B115200);
	tio.c_cc[VMIN] = 0;
	tio.c_cc[VTIME] = 0;
	tcsetattr(fd, TCSANOW, &tio);
	return fd;
}

/* ---------- Unix socket 监听 ---------- */
static int unix_listen(const char *path)
{
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	struct sockaddr_un a;
	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	strncpy(a.sun_path, path, sizeof(a.sun_path) - 1);
	unlink(path);
	if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) {
		perror("bind");
		exit(1);
	}
	listen(fd, 8);
	int fl = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, fl | O_NONBLOCK);
	return fd;
}

/* ---------- 广播给所有GUI ---------- */
static void bcast(const char *s)
{
	for (int i = 0; i < nclient; i++) {
		if (write(clients[i], s, strlen(s)) < 0) {
			close(clients[i]);
			clients[i] = clients[--nclient];
			i--;
		}
	}
}

/* ---------- 每来一个有效样本 ---------- */
static void on_sample(uint32_t ir, uint32_t red)
{
	fprintf(stderr, "[J] on_sample ir=%u red=%u nbuf=%d since=%d\n", ir, red, nbuf, since_run);
	fflush(stderr);
	char line[64];
	snprintf(line, sizeof line, "S %u %u\n", ir, red);
	bcast(line); /* 原始样本 -> GUI画波形 */

	/* 维护500长度的滑动窗口 */
	if (nbuf == BUFFER_SIZE) {
		memmove(ir_raw, ir_raw + 1, (BUFFER_SIZE - 1) * sizeof(uint32_t));
		memmove(red_raw, red_raw + 1, (BUFFER_SIZE - 1) * sizeof(uint32_t));
		nbuf--;
	}
	ir_raw[nbuf] = ir;
	red_raw[nbuf] = red;
	nbuf++;
	since_run++;

	if (nbuf == BUFFER_SIZE && since_run >= STEP) {
		int32_t hr = -999, spo2 = -999;
		int8_t hrv = 0, spv = 0;
		maxim_heart_rate_and_oxygen_saturation(ir_raw, BUFFER_SIZE, red_raw, &spo2, &spv, &hr, &hrv);
		if (hrv == 1) { // 算法认为结果有效
			if (hr >= 30 && hr <= 220) { // 心率合理范围（可根据实际调整）
				hr_history[hr_cnt % 5] = hr;
				hr_cnt++;
				if (hr_cnt >= 3) { // 至少攒够3个才开始用平均值
					int32_t sum = 0;
					int n = (hr_cnt < 5) ? hr_cnt : 5; // 取最近5个
					for (int i = 0; i < n; i++)
						sum += hr_history[i];
					hr = sum / n;
					hrv = 1; // 保持有效
				} else {
					hrv = 0; // 历史不足，先不显示
				}
			} else {
				hrv = 0; // 超出合理范围，视为无效
			}
		}
		char res[80];
		snprintf(res, sizeof res, "R %d %d %d %d\n", hr, (int)hrv, spo2, (int)spv);
		bcast(res);

		/* 丢掉最老的STEP个, 再收STEP个就重算 → 约每秒出一个结果 */
		memmove(ir_raw, ir_raw + STEP, (BUFFER_SIZE - STEP) * sizeof(uint32_t));
		memmove(red_raw, red_raw + STEP, (BUFFER_SIZE - STEP) * sizeof(uint32_t));
		nbuf = BUFFER_SIZE - STEP;
		since_run = 0;
	}
}

/* ---------- 帧解析状态机(找同步头+CRC) ---------- */
typedef enum { ST_HEAD1, ST_HEAD2, ST_TYPE, ST_LEN, ST_DATA, ST_CRC } pstate;
static pstate st = ST_HEAD1;
static uint8_t fbuf[13];
static int fpos = 0;
static int flen = 0;

static int feed_byte(uint8_t b)
{
	switch (st) {
	case ST_HEAD1:
		fbuf[0] = b;
		fpos = 1;
		if (b != HEAD1)
			return 0; /* 继续找 0xAA */
		st = ST_HEAD2;
		break;
	case ST_HEAD2:
		fbuf[1] = b;
		if (b == HEAD2)
			st = ST_TYPE;
		else {
			st = ST_HEAD1;
			return feed_byte(b);
		} /* 重新搜头 */
		break;
	case ST_TYPE:
		fbuf[2] = b;
		st = ST_LEN;
		break;
	case ST_LEN:
		fbuf[3] = b;
		flen = b;
		if (b == PAYLOAD_LEN) {
			fpos = 4;
			st = ST_DATA;
		} else {
			st = ST_HEAD1;
			if (b == HEAD1)
				return feed_byte(b);
		}
		break;
	case ST_DATA:
		fbuf[fpos++] = b;
		if (fpos == 4 + flen)
			st = ST_CRC;
		break;
	case ST_CRC:
		fbuf[12] = b;
		st = ST_HEAD1;
		{
			uint8_t sum = 0;
			for (int i = 0; i < 12; i++)
				sum += fbuf[i];
			if (sum == b && fbuf[2] == TYPE_HR)
				return 1; /* 完整有效帧 */
		}
		break;
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/ttyS1";
	const char *sockpath = argc > 2 ? argv[2] : "/tmp/max30102.sock";

	sfd = open_serial(dev);
	lsock = unix_listen(sockpath);
	printf("reading %s -> publishing on %s\n", dev, sockpath);
	fflush(stdout); /* 让这一行立即输出 */

	for (;;) {
		/* 打印：进入循环 */
		fprintf(stderr, "[A] enter loop\n");
		fflush(stderr);

		fd_set rf;
		FD_ZERO(&rf);
		FD_SET(sfd, &rf);
		FD_SET(lsock, &rf);
		int maxfd = sfd > lsock ? sfd : lsock;

		/* 打印：select 之前 */
		fprintf(stderr, "[B] before select\n");
		fflush(stderr);

		int sel_ret = select(maxfd + 1, &rf, NULL, NULL, NULL);

		/* 打印：select 之后 */
		fprintf(stderr, "[C] after select, ret=%d\n", sel_ret);
		fflush(stderr);

		if (FD_ISSET(lsock, &rf)) { /* 收新GUI连接 */
			fprintf(stderr, "[D] accept triggered\n");
			fflush(stderr);
			int c = accept(lsock, NULL, NULL);
			if (c >= 0) {
				fprintf(stderr, "[E] accept = %d\n", c);
				fflush(stderr);
				int fl = fcntl(c, F_GETFL, 0);
				fcntl(c, F_SETFL, fl | O_NONBLOCK);
				if (nclient < 8)
					clients[nclient++] = c;
				else
					close(c);
			}
		}
		if (FD_ISSET(sfd, &rf)) { /* 读串口数据 */
			fprintf(stderr, "[F] serial readable\n");
			fflush(stderr);
			uint8_t buf[512];
			ssize_t n = read(sfd, buf, sizeof buf);
			fprintf(stderr, "[G] read %zd bytes\n", n);
			fflush(stderr);
			if (n > 0) {
				for (ssize_t i = 0; i < n; i++) {
					if (feed_byte(buf[i])) {
						fprintf(stderr, "[H] got full frame\n");
						fflush(stderr);
						uint32_t ir = ((uint32_t)fbuf[4] << 24) | ((uint32_t)fbuf[5] << 16) |
							      ((uint32_t)fbuf[6] << 8) | fbuf[7];
						uint32_t red = ((uint32_t)fbuf[8] << 24) | ((uint32_t)fbuf[9] << 16) |
							       ((uint32_t)fbuf[10] << 8) | fbuf[11];
						on_sample(ir, red);
						fprintf(stderr, "[I] after on_sample\n");
						fflush(stderr);
					}
				}
			}
		}
	}
	return 0;
}
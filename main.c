#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define rassert(x) if (!(x)) return false

bool read_msg(FILE *f, uint8_t *type, int32_t *tick, uint8_t *slot, void **data, size_t *data_len) {
	char buf[6];

	if (fread(buf, 1, sizeof buf, f) != sizeof buf) {
		return false;
	}

	*type = (uint8_t)buf[0];
	*tick = *(int32_t *)&buf[1];
	*slot = (uint8_t)buf[5];

	uint32_t len;

	switch (*type) {
	case 1: // SignOn
	case 2: // Packet
		rassert(fseek(f, 76*2 + 8, SEEK_CUR) != -1);
		rassert(fread(&len, 4, 1, f) == 1);
		rassert(fseek(f, -(76*2 + 12), SEEK_CUR) != -1);
		len += 76*2 + 12;
		break;

	case 3: // SyncTick
		len = 0;
		break;

	case 4: // ConsoleCmd
		rassert(fread(&len, 4, 1, f) == 1);
		rassert(fseek(f, -4, SEEK_CUR) != -1);
		len += 4;
		break;

	case 5: // UserCmd
		rassert(fseek(f, 4, SEEK_CUR) != -1);
		rassert(fread(&len, 4, 1, f) == 1);
		rassert(fseek(f, -8, SEEK_CUR) != -1);
		len += 8;
		break;

	case 6: // DataTables
		rassert(fread(&len, 4, 1, f) == 1);
		rassert(fseek(f, -4, SEEK_CUR) != -1);
		len += 4;
		break;

	case 7: // Stop
		len = 0; // not strictly right but eh
		break;

	case 8: // CustomData
		rassert(fseek(f, 4, SEEK_CUR) != -1);
		rassert(fread(&len, 4, 1, f) == 1);
		rassert(fseek(f, -8, SEEK_CUR) != 1);
		len += 8;
		break;

	case 9: // StringTables
		rassert(fread(&len, 4, 1, f) == 1);
		rassert(fseek(f, -4, SEEK_CUR) != -1);
		len += 4;
		break;

	default:
		fputs("malformed demo message\n", stderr);
		return false;
	}

	*data_len = len;

	if (len != 0) {
		*data = malloc(len);
		if (fread(*data, 1, len, f) != len) {
			free(*data);
			return false;
		}
	}

	return true;
}

bool write_msg(FILE *f, uint8_t type, int32_t tick, uint8_t slot, void *data, size_t data_len) {
	uint8_t hdr[6];
	hdr[0] = type;
	*(int32_t *)&hdr[1] = tick;
	hdr[5] = slot;

	rassert(fwrite(hdr, 1, sizeof hdr, f) == sizeof hdr);

	if (data_len != 0) {
		rassert(fwrite(data, 1, data_len, f) == data_len);
	}

	return true;
}

bool repair(FILE *in, FILE *out) {
	uint8_t hdr[1072];
	if (fread(hdr, 1, sizeof hdr, in) != sizeof hdr) {
		fputs("incomplete demo header\n", stderr);
		return false;
	}

	if (strncmp((const char *)hdr, "HL2DEMO\0", 8)) {
		fputs("invalid demo file\n", stderr);
		return false;
	}

	// TODO: we should probably adjust the playback time/ticks/frames
	
	if (fwrite(hdr, 1, sizeof hdr, out) != sizeof hdr) {
		fputs("failed to write demo header\n", stderr);
		return false;
	}

	fpos_t msg_pos;
	fgetpos(in, &msg_pos);
	
	int start_tick = INT_MIN;

	while (start_tick == INT_MIN) {
		uint8_t type, slot;
		int32_t tick;
		void *data;
		size_t data_len;

		if (!read_msg(in, &type, &tick, &slot, &data, &data_len)) {
			fputs("failed to find demo start message\n", stderr);
			return false;
		}

		if ((type == 1 || type == 2) && tick > 0) { // SignOn or Packet
			start_tick = tick + 1;
		}
	}

	fsetpos(in, &msg_pos);

	int last_tick = INT_MIN;

	while (1) {
		uint8_t type, slot;
		int32_t tick;
		void *data;
		size_t data_len;

		if (!read_msg(in, &type, &tick, &slot, &data, &data_len)) {
			break;
		}

		if (type == 7) { // Stop
			break;
		}

		int32_t new_tick = tick - start_tick;

		last_tick = new_tick;

		if (!write_msg(out, type, new_tick, slot, data, data_len)) {
			fputs("failed to write demo message\n", stderr);
			if (data_len != 0) free(data);
			return false;
		}

		if (type == 4 && data_len == 23 && !strcmp((char *)data + 4, "leaderboard_open 1")) {
			free(data);
			break;
		}

		if (data_len != 0) free(data);
	}

	int stop_tick = last_tick == INT_MIN ? 0 : last_tick + 1;

	// write stop message
	if (!write_msg(out, 7, stop_tick, 0, NULL, 0)) {
		fputs("failed to write stop message\n", stderr);
		return false;
	}

	return true;
}

int main(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "usage: %s <input filename> <output filename>\n", argv[0]);
		return 1;
	}

	FILE *in = fopen(argv[1], "rb");
	if (!in) {
		fprintf(stderr, "could not open file %s for reading\n", argv[1]);
		return 1;
	}

	FILE *out = fopen(argv[2], "wb");
	if (!in) {
		fprintf(stderr, "could not open file %s for writing\n", argv[2]);
		fclose(in);
		return 1;
	}

	if (repair(in, out)) {
		puts("successfully repaired demo file");
	}

	fclose(in);
	fclose(out);

	return 0;
}

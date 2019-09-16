#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include "../simplemotion.h"

static int verify_bytes(char* buffer, size_t len, char expected);
static void hexdump(char* buffer, size_t len);

int main(void) {
	const size_t capacity = 1024;
	char buffer[capacity];
	// keep filling the buffer with garbage to have strcmp go over the buffer if
	// a null byte is missed
	memset(buffer, 0xaa, capacity);

	{
		assert(smDescribeSmStatus(buffer, 0, SM_NONE) == 4); // return value is how many would had needed to be written (at least)
		assert(buffer[0] == 0); // not sure if this is similar to any snprintf operation, but handy
		assert(verify_bytes(&buffer[1], capacity - 1, 0xaa) == 0);
	}

	{
		assert(smDescribeSmStatus(buffer, 3, SM_NONE) == 4); // return value is how many would had needed to be written (at least)
		assert(strcmp("NO", buffer) == 0);
		assert(verify_bytes(&buffer[3], capacity - 3, 0xaa) == 0);
	}

	{
		assert(smDescribeSmStatus(buffer, capacity, SM_NONE) == 4);
		assert(memcmp("NONE", buffer, 4) == 0);
	}

	{
		memset(buffer, 0xaa, capacity);
		assert(smDescribeSmStatus(buffer, capacity, SM_OK | SM_ERR_NODEVICE | SM_ERR_BUS | SM_ERR_COMMUNICATION | SM_ERR_PARAMETER | SM_ERR_LENGTH | (1 << 18)) > 0);
		const char* expected = "OK ERR_NODEVICE ERR_BUS ERR_COMMUNICATION ERR_PARAMETER ERR_LENGTH EXTRA(262144)";
		assert(strcmp(buffer, expected) == 0);
	}

	{
		memset(buffer, 0xaa, capacity);
		assert(smDescribeFault(buffer, capacity, FLT_FOLLOWERROR | FLT_ENCODER | FLT_ALLOC | (1 << 17)) > 0);
		const char* expected = "FOLLOWERROR ENCODER PROGRAM_OR_MEM EXTRA(131072)";
		assert(strcmp(buffer, expected) == 0);
	}

	{
		memset(buffer, 0xaa, capacity);
		assert(smDescribeStatus(buffer, capacity, STAT_RUN | STAT_ENABLED | STAT_FAULTSTOP | STAT_SAFE_TORQUE_MODE_ACTIVE | (1 << 19)) > 0);
		const char* expected = "RUN ENABLED FAULTSTOP SAFE_TORQUE_MODE_ACTIVE EXTRA(524288)";
		assert(strcmp(buffer, expected) == 0);
	}

	{
		memset(buffer, 0xaa, capacity);
		assert(smDescribeStatus(buffer, capacity, 0) == 0);
		assert(buffer[0] == 0);
	}

	{
		memset(buffer, 0xaa, capacity);
		assert(smDescribeFault(buffer, capacity, 0) == 0);
		assert(buffer[0] == 0);
	}
}

static void hexdump(char* buffer, size_t len) {
	int all_aa = 0;
	for (size_t i = 0; i < len; ++i) {
		if (i % 16 == 0) {
			if (all_aa == 16) {
				//break;
			}
			all_aa = 0;
			printf("\n%04lx | ", i);
		}

		printf("%02hhx ", buffer[i]);
		if ((unsigned char)buffer[i] == 0xaa) {
			all_aa++;
		}
	}

	printf("\n");

}

static int verify_bytes(char* buffer, size_t len, char expected) {
	for (size_t i = 0; i < len; ++i) {
		if (buffer[i] != expected) {
			return i + 1;
		}
	}
	return 0;
}

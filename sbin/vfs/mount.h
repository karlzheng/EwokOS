#ifndef MOUNT_H
#define MOUNT_H

#include <types.h>

typedef enum {
	DEV_NONE = 0,
	DEV_SRAMDISK,
	DEV_TTY,
} DeviceTypeT;

struct TreeNode;

typedef struct {
	uint32_t type;
	char device[NAME_MAX+1];

	struct TreeNode* to;
} MountT;

typedef struct {
	void (*mount)(TreeNodeT*);
	int (*open)(const char*);
	void (*close)(int);
	int (*write)(int, const char*, int);
	int (*read)(int, char*, int);
} DevTypeT;

void mountInit();

MountT* getMountInfo(struct TreeNode* node);

DevTypeT* getDevInfo(struct TreeNode* node);


#endif
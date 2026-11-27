// taihen

int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid, uint32_t funcnid, uintptr_t *func);

// SceSblAuthMgr

typedef struct SceSelfInfo // size is 0x90
{
	SceUInt64 program_authority_id;
	SceUInt64 padding1;
	uint8_t capability[0x20];
	uint8_t attribute[0x20];
	uint8_t padding2[0x10];
	uint8_t klicensee[0x10];
	uint32_t unk_70;
	uint32_t unk_74;
	uint32_t unk_78;
	uint32_t unk_7C;
	uint32_t unk_80; // ex: 0x10
	uint32_t unk_84;
	uint32_t unk_88;
	uint32_t unk_8C;
} SceSelfInfo;

typedef struct SceSblSmCommContext130 // size is 0x130 as its name indicates
{
	uint32_t unk_0;
	uint32_t self_type; // kernel = 0, user = 1, SM = 2
	SceSelfInfo caller_self_auth_info; // can be obtained with sceKernelGetSelfInfoForKernel
	SceSelfInfo called_self_auth_info; // set by F00D in F00D SceSblSmCommContext130 response
	uint32_t path_id; // can be obtained with sceSblACMgrGetPathIdForKernel or sceIoGetPathIdExForDriver
	uint32_t unk_12C;
} SceSblSmCommContext130;

int (*sceSblAuthMgrSmStartForKernel)(int* ctx);
void *(*sceSysmemMallocForKernel)(size_t size);
int (*sceSysmemFreeForKernel)(void *ptr);
int (*sceSblAuthMgrAuthHeaderForKernel)(int ctx, char *header, int header_size, SceSblSmCommContext130 *context_130);
int (*sceSblAuthMgrSmFinalizeForKernel)(int ctx);
int (*sceSblAuthMgrLoadSelfSegmentForKernel)(int ctx, int segment_number, size_t segment_size, void *output_buffer, size_t program_size);
int (*sceSblAuthMgrLoadSelfBlockForKernel)(int ctx, char *buffer, int offset);
int (*sceIoGetPathIdExForDriver)(SceUID pid, const char *path, int ignored, int *pathId);

// SceNpDrm

/* typedef removed */

/* removed: provided by vitasdk header */
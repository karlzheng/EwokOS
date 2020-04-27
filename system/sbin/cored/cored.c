#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/ipc.h>
#include <fsinfo.h>
#include <sys/proc.h>
#include <sys/syscall.h>
#include <sys/kserv.h>
#include <sys/core.h>
#include <hashmap.h>
#include <kevent.h>
#include <usinterrupt.h>

static map_t* _global = NULL;

static proto_t* global_get(const char* key) {
	proto_t* ret;
	hashmap_get(_global, (char*)key, (void**)&ret);
	return ret;
}

static proto_t* global_set(const char* key, void* data, uint32_t size) {
	proto_t* v = global_get(key);
	if(v != NULL) {
		proto_copy(v, data, size);
	}
	else {
		v = proto_new(data, size);
		hashmap_put(_global, (char*)key, v);
	}
	return v;
}

static void global_del(const char* key) {
	proto_t* v = global_get(key);
	hashmap_remove(_global, (char*)key);
	proto_free(v);
}

static void do_global_set(proto_t* in) {
	const char* key = proto_read_str(in);
	int32_t size;
	void* data = proto_read(in, &size);
	if(data != NULL) {
		global_set(key, data, size);
	}
}

static void do_global_get(proto_t* in, proto_t* out) {
	const char* key = proto_read_str(in);
	proto_t * v= global_get(key);
	if(v != NULL)
		proto_copy(out, v->data, v->size);
}

static void do_global_del(proto_t* in) {
	const char* key = proto_read_str(in);
	global_del(key);
}

static int _kservs[KSERV_MAX]; //pids of kservers

static void do_reg(int pid, proto_t* in, proto_t* out) {
	int ks_id = proto_read_int(in);
	if(ks_id < 0 || ks_id >= KSERV_MAX || _kservs[ks_id] >= 0) {
		proto_add_int(out, -1);
		return;
	}
	_kservs[ks_id] = pid;
	proto_add_int(out, 0);
}

static void do_get(proto_t* in, proto_t* out) {
	int ks_id = proto_read_int(in);
	if(ks_id < 0 || ks_id >= KSERV_MAX || _kservs[ks_id] < 0) {
		proto_add_int(out, -1);
		return;
	}
	proto_add_int(out, _kservs[ks_id]);
}

static void do_unreg(int pid, proto_t* in, proto_t* out) {
	int ks_id = proto_read_int(in);
	if(ks_id < 0 || ks_id >= KSERV_MAX ||
			_kservs[ks_id] < 0 ||
			_kservs[ks_id] != pid) {
		proto_add_int(out, -1);
		return;
	}
	proto_add_int(out, 0);
}

static void handle_ipc(int pid, int cmd, proto_t* in, proto_t* out, void* p) {
	(void)p;

	switch(cmd) {
	case CORE_CMD_KSERV_REG: //regiester kserver pid
		do_reg(pid, in, out);
		return;
	case CORE_CMD_KSERV_UNREG: //unregiester kserver pid
		do_unreg(pid, in, out);
		return;
	case CORE_CMD_KSERV_GET: //get kserver pid
		do_get(in, out);
		return;
	case CORE_CMD_GLOBAL_SET:
		do_global_set(in);
		return;
	case CORE_CMD_GLOBAL_DEL: 
		do_global_del(in);
		return;
	case CORE_CMD_GLOBAL_GET:
		do_global_get(in, out);
		return;
	}
}

/*----kernel event -------*/
static void do_fsclosed(proto_t *data) {
	fsinfo_t fsinfo;
	int32_t to_pid = proto_read_int(data);
	int32_t from_pid = proto_read_int(data);
	int32_t fd = proto_read_int(data);
	int32_t fuid = proto_read_int(data);
	proto_read_to(data, &fsinfo, sizeof(fsinfo_t));

	proto_t in;
	proto_init(&in, NULL, 0);
	proto_add_int(&in, fd);
	proto_add_int(&in, fuid);
	proto_add_int(&in, from_pid);
	proto_add(&in, &fsinfo, sizeof(fsinfo_t));

	ipc_call(to_pid, FS_CMD_CLOSED, &in, NULL);
	proto_clear(&in);
}

static void do_usint_ps2_key(proto_t* data) {
	int32_t key_scode = proto_read_int(data);
	int32_t pid = _kservs[KSERV_PS2_KEYB];
	if(pid < 0)
		return;

	proto_t in;
	proto_init(&in, NULL, 0);
	proto_add_int(&in, key_scode);
	ipc_call(pid, IPC_SAFE_CMD_BASE, &in, NULL);
	proto_clear(&in);
}

static void do_user_space_int(proto_t *data) {
	int32_t usint = proto_read_int(data);
	switch(usint) {
	case US_INT_PS2_KEY:
		do_usint_ps2_key(data);
		return;
	}
}

static void handle_event(kevent_t* kev) {
	if(kev->type == KEV_FCLOSED) {
		do_fsclosed(kev->data);
	}
	if(kev->type == KEV_US_INT) {
		do_user_space_int(kev->data);
	}
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;

	_global = hashmap_new();

	for(int i=0; i<KSERV_MAX; i++) {
		_kservs[i] = -1;
	}
	
	kserv_run(handle_ipc, NULL, true);

	while(1) {
		kevent_t* kev = (kevent_t*)syscall0(SYS_GET_KEVENT);
		if(kev != NULL) {
			handle_event(kev);
			if(kev->data != NULL)
				proto_free(kev->data);
			free(kev);
		}
		usleep(0);
	}

	hashmap_free(_global);
	return 0;
}
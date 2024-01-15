#pragma once

#include "ll.h"
#include <stdio.h>
#include <sys/socket.h>

#define LOGF(...) printf(__VA_ARGS__)

#ifdef DEBUG
#define DLOGF(...) LOGF(__VA_ARGS__)
#else
#define DLOGF(...)
#endif

typedef unsigned char AuthMethod;
typedef unsigned char Cmd;
typedef unsigned char Atyp;
typedef unsigned char Rep;

struct S5ServerCtx {
	LL *userpass;
	int flags;
};

typedef struct S5ServerCtx S5ServerCtx;

enum S5ServerFlags {
	FLAG_NO_AUTH = 1 << 0,
	FLAG_USERPASS_AUTH = 1 << 1,
};

enum AuthMethods {
	NO_AUTH             = 0,
	USERPASS_AUTH       = 2,
	INVALID_AUTH_METHOD = 0xff,
};

enum Cmds {
	CMD_CONNECT       = 1,
	CMD_BIND          = 2,
	CMD_UDP_ASSOCIATE = 3,
};

enum Atyps {
	ATYP_IPV4         = 1,
	ATYP_DOMAIN_NAME  = 3,
	ATYP_IPV6         = 4,
};

enum Reps {
	REP_OK                  = 0,
	REP_FAIL                = 1,
	REP_NETWORK_UNREACHABLE = 3,
	REP_HOST_UNREACHABLE    = 4,
	REP_CONNECTION_REFUSED  = 5,
	REP_CMD_NOT_SUPPORTED   = 7,
	REP_ATYP_NOT_SUPPORTED  = 8,
};

// returns 0 on success
int s5_server_ctx_init(S5ServerCtx *ctx);
void s5_server_ctx_free(S5ServerCtx *ctx);
// returns 0 on success
int s5_server_add_userpass(S5ServerCtx *ctx, char *userpass);
void s5_server_handler(const S5ServerCtx *ctx, int fd);

// returns -1 on error
int s5_create_tcp_server(const char *host, const char *port, int backlog);

char *s5_atyp_str(Atyp atyp);
char *s5_cmd_str(Cmd cmd);
char *s5_rep_str(Rep rep);
char *s5_auth_method_str(AuthMethod method);

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

struct Socks5ServerCtx {
	LL *userpass;
	int flags;
};

typedef struct Socks5ServerCtx Socks5ServerCtx;

enum Socks5ServerFlags {
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
int socks5_server_ctx_init(Socks5ServerCtx *ctx);
void socks5_ctx_free(Socks5ServerCtx *ctx);
// returns 0 on success
int socks5_server_add_userpass(const Socks5ServerCtx *ctx, char *userpass);
// returns 0 on success
int socks5_server_auth_userpass(const Socks5ServerCtx *ctx, int fd);
void socks5_handler(const Socks5ServerCtx *ctx, int fd);

// returns -1 on error
int create_tcp_server(const char *host, const char *port, int backlog);
void bridge_fd(int fd1, int fd2);

AuthMethod choose_auth_method(const Socks5ServerCtx *ctx, AuthMethod *methods, unsigned char nmethods);
// returns 0 on success
int negotiate_auth_method(const Socks5ServerCtx *ctx, int fd, AuthMethod *method);
// returns 0 on success
int handle_auth_method(const Socks5ServerCtx *ctx, int fd, AuthMethod method);

// returns 0 on success
int get_request(const Socks5ServerCtx *ctx, int fd, Cmd *cmd, Atyp *atyp, struct sockaddr_storage *sa, Rep *rep);
// returns 0 on success
int reply_request(const Socks5ServerCtx *ctx, int fd, Rep rep, Atyp atyp, struct sockaddr_storage *sa);
// returns -1 on error
int connect_dst(const Socks5ServerCtx *ctx, Atyp atyp, struct sockaddr_storage *sa, int type, int proto);

// is_valid* returns non zero on success
int is_valid_cmd(Cmd cmd);
int is_valid_atyp(Atyp atyp);

char *atyp_str(Atyp atyp);
char *cmd_str(Cmd cmd);
char *rep_str(Rep rep);
char *auth_method_str(AuthMethod method);
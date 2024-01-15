#include "rfc1928.h"
#include "ll.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int is_valid_cmd(S5Cmd cmd);
static int is_valid_atyp(S5Atyp atyp);
static int negotiate_auth_method(const S5ServerCtx *ctx, int fd, S5AuthMethod *method);
static int handle_auth_method(const S5ServerCtx *ctx, int fd, S5AuthMethod method);
static S5AuthMethod choose_auth_method(const S5ServerCtx *ctx, S5AuthMethod *methods, unsigned char nmethods);
static int auth_userpass(const S5ServerCtx *ctx, int fd);
static void bridge_fd(int fd1, int fd2);
static int connect_dst(const S5ServerCtx *ctx, S5Atyp atyp, struct sockaddr_storage *sa, int type, int proto);
static int get_request(const S5ServerCtx *ctx, int fd, S5Cmd *cmd, S5Atyp *atyp, struct sockaddr_storage *sa, S5Rep *rep);
static int reply_request(const S5ServerCtx *ctx, int fd, S5Rep rep, S5Atyp atyp, struct sockaddr_storage *sa);

void s5_server_ctx_free(S5ServerCtx *ctx)
{
	LLNode *node = ctx->userpass->head;
	while (node) {
		free(node->data);
		node = node->next;
	}
	ll_free(ctx->userpass);
}

int s5_server_ctx_init(S5ServerCtx *ctx)
{
	ctx->flags = 0;
	ctx->userpass = ll_create();
	if (ctx->userpass == NULL) return 1;
	return 0;
}

int s5_server_add_userpass(S5ServerCtx *ctx, char *userpass)
{
	size_t len = strlen(userpass);
	char *buf = malloc(len + 1);
	if (buf == NULL) return 1;
	memcpy(buf, userpass, len + 1);
	return ll_append(ctx->userpass, buf);
}

static int auth_userpass(const S5ServerCtx *ctx, int fd)
{
	char userpass[256 + 256 + 2];
	userpass[0] = 0;
	char *tmp = userpass;
	unsigned char len, ver, status = 1;

	if (read(fd, &ver, sizeof(ver)) != sizeof(ver)) return 1;
	if (read(fd, &len, sizeof(len)) != sizeof(len)) return 1;
	if (read(fd, userpass, len) != len) return 1;
	tmp += len;
	*tmp++ = ':';
	if (read(fd, &len, sizeof(len)) != sizeof(len)) return 1;
	if (read(fd, tmp, len) != len) return 1;
	tmp += len;
	*tmp = 0;

	LLNode *node = ctx->userpass->head;
	while (node) {
		if (strcmp(node->data, userpass) == 0) {
			status = 0;
			break;
		}
		node = node->next;
	}

	if (ver != 1) status = 1;
	if (write(fd, &ver, sizeof(ver)) != sizeof(ver)) return 1;
	if (write(fd, &status, sizeof(status)) != sizeof(status)) return 1;

	return status == 0 ? 0 : 1;
}

void s5_server_handler(const S5ServerCtx *ctx, int fd)
{
	S5AuthMethod method;

	if (negotiate_auth_method(ctx, fd, &method) != 0) {
		S5DLOGF("auth negotiation failed\n");
		return;
	}

	S5DLOGF("method: %s\n", s5_auth_method_str(method));

	if (method == S5INVALID_AUTH_METHOD) return;

	int dstfd = -1;
	S5Cmd cmd;
	S5Atyp atyp;
	S5Rep rep;
	struct sockaddr_storage sa;

	if (get_request(ctx, fd, &cmd, &atyp, &sa, &rep) != 0) {
		S5DLOGF("get_request failed\n");
		return;
	}

	S5DLOGF("cmd: %s atyp: %s\n", s5_cmd_str(cmd), s5_atyp_str(atyp));

	if (rep != S5REP_OK) goto reply;

	if (!is_valid_cmd(cmd))   rep = S5REP_CMD_NOT_SUPPORTED;
	if (!is_valid_atyp(atyp)) rep = S5REP_ATYP_NOT_SUPPORTED;

	if (rep != S5REP_OK) goto reply;

	errno = 0;
	dstfd = connect_dst(ctx, atyp, &sa, SOCK_STREAM, IPPROTO_TCP);
	if (dstfd == -1) {
		S5DLOGF("connect_dst failed\n");
		switch (errno) {
		case ENETUNREACH:
			rep = S5REP_NETWORK_UNREACHABLE;
		case ECONNREFUSED:
			rep = S5REP_CONNECTION_REFUSED;
		default:
			rep = S5REP_FAIL;
		}
	}

reply:

	S5DLOGF("replying: %s\n", s5_rep_str(rep));

	if (reply_request(ctx, fd, rep, atyp, &sa) != 0) {
		S5DLOGF("reply failed\n");
		return;
	}

	if (dstfd == -1 || rep != S5REP_OK) return;

	bridge_fd(fd, dstfd);

	return;
}

static void bridge_fd(int fd1, int fd2)
{
	char buf[4096];
	ssize_t rn1, rn2, wn1, wn2, lrn1, lrn2;

	lrn1 = lrn2 = 0;

	struct pollfd fds[2];

	fds[0].fd     = fd1;
	fds[0].events = POLLIN;
	fds[1].fd     = fd2;
	fds[1].events = POLLIN;

	while (1) {
		rn1 = rn2 = fds[0].revents = fds[1].revents = 0;

		int e = poll(fds, 2, 2000);
		if (e == -1) return;

		if ((fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) ||
		    (fds[1].revents & (POLLHUP | POLLERR | POLLNVAL)))
			return;

		if (fds[0].revents & POLLIN) {
			rn1 = read(fd1, buf, sizeof(buf));
			if (rn1 == -1) return;
			wn2 = write(fd2, buf, rn1);
			if (wn2 != rn1) return;
		}

		if (fds[1].revents & POLLIN) {
			rn2 = read(fd2, buf, sizeof(buf));
			if (rn2 == -1) return;
			wn1 = write(fd1, buf, rn2);
			if (wn1 != rn2) return;
		}

		if ((rn1 | lrn1 | rn2 | lrn2) == 0) return;

		lrn1 = rn1;
		lrn2 = rn2;
	}
}

static int is_valid_cmd(S5Cmd cmd)
{
	switch (cmd) {
	case S5CMD_CONNECT:
		return 1;
	default:
		return 0;
	}
}

static int is_valid_atyp(S5Atyp atyp)
{
	switch (atyp) {
	case S5ATYP_IPV4:
	case S5ATYP_IPV6:
	case S5ATYP_DOMAIN_NAME:
		return 1;
	default:
		return 0;
	}
}

char *s5_atyp_str(S5Atyp atyp)
{
	switch (atyp) {
	case S5ATYP_IPV4:        return "IPV4";
	case S5ATYP_IPV6:        return "IPV6";
	case S5ATYP_DOMAIN_NAME: return "DOMAIN NAME";
	default:                 return "?";
	}
}

char *s5_rep_str(S5Rep rep)
{
	switch (rep) {
	case S5REP_OK:                 return "OK";
	case S5REP_FAIL:               return "FAIL";
	case S5REP_ATYP_NOT_SUPPORTED: return "ATYP NOT SUPPORTED";
	case S5REP_CMD_NOT_SUPPORTED:  return "CMD NOT SUPPORTED";
	case S5REP_HOST_UNREACHABLE:   return "HOST UNREACHABLE";
	case S5REP_CONNECTION_REFUSED: return "CONNECTION REFUSED";
	default:                       return "?";
	}
}

char *s5_auth_method_str(S5AuthMethod method)
{
	switch (method) {
	case S5USERPASS_AUTH:       return "USER:PASS AUTH";
	case S5NO_AUTH:             return "NO AUTH";
	case S5INVALID_AUTH_METHOD: return "INVALID AUTH METHOD";
	default:                    return "?";
	}
}

char *s5_cmd_str(S5Cmd cmd)
{
	switch (cmd) {
	case S5CMD_BIND:    return "BIND";
	case S5CMD_CONNECT: return "CONNECT";
	default:            return "?";
	}
}

static int connect_dst(const S5ServerCtx *ctx, S5Atyp atyp, struct sockaddr_storage *sa, int type, int proto)
{
	switch (atyp) {
	case S5ATYP_IPV4:
	case S5ATYP_IPV6:
		break;
	default:
		return -1;
	}

	int fd = socket(sa->ss_family, type, proto);
	if (fd == -1) return -1;

	if (atyp == S5ATYP_IPV4) {
		if (connect(fd, (struct sockaddr *)sa, sizeof(struct sockaddr_in)) != 0) {
			close(fd);
			return -1;
		}
	} else {
		if (connect(fd, (struct sockaddr *)sa, sizeof(struct sockaddr_in6)) != 0) {
			close(fd);
			return -1;
		}
	}

	return fd;
}

static int reply_request(const S5ServerCtx *ctx, int fd, S5Rep rep, S5Atyp atyp, struct sockaddr_storage *sa)
{
	unsigned char ver = 5, rsv = 0;

	if (write(fd, &ver, sizeof(ver)) != sizeof(ver)) return 1;
	if (write(fd, &rep, sizeof(rep)) != sizeof(rep)) return 1;
	if (write(fd, &rsv, sizeof(rsv)) != sizeof(rsv)) return 1;
	if (write(fd, &atyp, sizeof(atyp)) != sizeof(atyp)) return 1;

	switch (sa->ss_family) {
	case AF_INET:
		if (write(fd, &((struct sockaddr_in *)sa)->sin_addr, 4) != 4) return 1;
		if (write(fd, &((struct sockaddr_in *)sa)->sin_port, 2) != 2) return 1;
		break;
	case AF_INET6:
		if (write(fd, &((struct sockaddr_in6 *)sa)->sin6_addr, 8) != 8) return 1;
		if (write(fd, &((struct sockaddr_in6 *)sa)->sin6_port, 2) != 2) return 1;
		break;
	default:
		return 1;
	}

	return 0;
}

static int get_request(const S5ServerCtx *ctx, int fd, S5Cmd *cmd, S5Atyp *atyp, struct sockaddr_storage *sa, S5Rep *rep)
{
	*rep = S5REP_FAIL;
	bzero(sa, sizeof(*sa));
	struct addrinfo *ainfo, *tmp;
	unsigned char hostlen;
	char host[257] = { 0 };
	unsigned char ver, rsv;

	if (read(fd, &ver, sizeof(ver)) != sizeof(ver)) return 1;
	if (read(fd, cmd, sizeof(*cmd)) != sizeof(*cmd)) return 1;
	if (read(fd, &rsv, sizeof(rsv)) != sizeof(rsv)) return 1;
	if (read(fd, atyp, sizeof(*atyp)) != sizeof(*atyp)) return 1;
	if (ver != 5) return 0;

	switch (*atyp) {
	case S5ATYP_IPV4:
		sa->ss_family = AF_INET;
		if (read(fd, &((struct sockaddr_in *)sa)->sin_addr, 4) != 4) return 1;
		if (read(fd, &((struct sockaddr_in *)sa)->sin_port, 2) != 2) return 1;
		break;
	case S5ATYP_IPV6:
		sa->ss_family = AF_INET6;
		if (read(fd, &((struct sockaddr_in6 *)sa)->sin6_addr, 8) != 8) return 1;
		if (read(fd, &((struct sockaddr_in6 *)sa)->sin6_port, 2) != 2) return 1;
		break;
	case S5ATYP_DOMAIN_NAME:
		if (read(fd, &hostlen, sizeof(hostlen)) != sizeof(hostlen)) return 1;
		if (read(fd, host, hostlen) != hostlen) return 1;
		if (getaddrinfo(host, NULL, NULL, &ainfo) != 0) return 0;

		tmp = ainfo;
		while (tmp) {
			if (tmp->ai_family == AF_INET6 || tmp->ai_family == AF_INET) break;
			tmp = tmp->ai_next;
		}

		if (tmp == NULL) {
			freeaddrinfo(ainfo);
			return 0;
		}

		memcpy(sa, ainfo->ai_addr, ainfo->ai_addrlen);

		if (tmp->ai_family == AF_INET) {
			*atyp = S5ATYP_IPV4;
			if (read(fd, &((struct sockaddr_in *)sa)->sin_port, 2) != 2) {
				freeaddrinfo(ainfo);
				return 1;
			}
		} else {
			*atyp = S5ATYP_IPV6;
			if (read(fd, &((struct sockaddr_in6 *)sa)->sin6_port, 2) != 2) {
				freeaddrinfo(ainfo);
				return 1;
			}
		}

		freeaddrinfo(ainfo);
		break;
	default:
		*rep = S5REP_ATYP_NOT_SUPPORTED;
		return 0;
	}

	*rep = S5REP_OK;
	return 0;
}

static S5AuthMethod choose_auth_method(const S5ServerCtx *ctx, S5AuthMethod *methods, unsigned char nmethods)
{
	if (ctx->flags & S5FLAG_NO_AUTH && memchr(methods, S5NO_AUTH, nmethods))
		return S5NO_AUTH;
	if (ctx->flags & S5FLAG_USERPASS_AUTH && memchr(methods, S5USERPASS_AUTH, nmethods))
		return S5USERPASS_AUTH;
	return S5INVALID_AUTH_METHOD;
}

static int handle_auth_method(const S5ServerCtx *ctx, int fd, S5AuthMethod method)
{
	switch (method) {
	case S5NO_AUTH:
		return 0;
	case S5USERPASS_AUTH:
		return auth_userpass(ctx, fd);
	default:
		return 1;
	}
}

static int negotiate_auth_method(const S5ServerCtx *ctx, int fd, S5AuthMethod *method)
{
	unsigned char ver, nmethods;
	S5AuthMethod methods[0xff], smethod;

	if (read(fd, &ver, sizeof(ver)) != sizeof(ver)) return 1;
	if (ver != 5) return 1;
	if (read(fd, &nmethods, sizeof(nmethods)) != sizeof(nmethods)) return 1;
	if (read(fd, methods, nmethods) != nmethods) return 1;

	smethod = choose_auth_method(ctx, methods, nmethods);

	if (write(fd, &ver, sizeof(ver)) != sizeof(ver)) return 1;
	if (write(fd, &smethod, sizeof(smethod)) != sizeof(smethod)) return 1;

	if (handle_auth_method(ctx, fd, smethod) != 0)
		return 1;

	*method = smethod;

	return 0;
}

int s5_create_server(const char *host, const char *port, int backlog, int proto)
{
	struct addrinfo hints, *res;
	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_protocol = proto;
	hints.ai_flags = AI_PASSIVE;

	if (getaddrinfo(host, port, &hints, &res) != 0)
		return -1;

	int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sockfd == -1)
		goto error;

	if (bind(sockfd, res->ai_addr, res->ai_addrlen) != 0) {
		close(sockfd);
		sockfd = -1;
		goto error;
	}

	if (res->ai_socktype != SOCK_DGRAM && listen(sockfd, backlog) != 0) {
		close(sockfd);
		sockfd = -1;
		goto error;
	}

error:

	freeaddrinfo(res);
	return sockfd;
}

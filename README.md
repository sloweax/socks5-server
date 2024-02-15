## Usage
```
usage: ./ss5 [OPTION...]
OPTION:
     -h                  shows usage and exits
     -v                  shows version and exits
     -n                  allow NO AUTH
     -u user:pass        add user:pass
     -U file             add all user:pass from file
     -p port             listen on port (1080 by default)
     -a addr             bind on addr (0.0.0.0 by default)
     -w workers          number of workers (4 by default)
```

## Build
Make sure you have `gcc` and `make` installed
```
git clone https://github.com/sloweax/ss5
cd ss5
make
```

## Supported socks5 features
- IPV4, IPV6, DOMAIN NAME Address types
- CONNECT command
- NO AUTH, multi USER:PASS authentication
- TCP

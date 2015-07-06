# httpdns
A simple httpdns server daemon using libevent2

```
HttpDns 0.1
Command line arguments with either long or short options:
     -h, --help     show this help.
     -v, --verbose     print log messages
     -d, --deamon     put program to daemon mode.
     -r, --resolv     use specified resolv file
     -l, --listen=<ip>     specify your local ip address listen at, default 0.0.0.0
     -p, --port=<ip>     specify your port listen at, default 9999
```

Now I am running an instance at:

[http://128.199.203.224/d?dn=www.google.com](http://128.199.203.224/d?dn=www.google.com)


## Compile
* Windows solution is just for debug & test, please set libevent2 includes/libs first

* Linux
* Download & compile & install libevent2 first, then

```shell
g++ http_dns.cpp -o http_dns -levent
```
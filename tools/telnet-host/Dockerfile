# A telnet host for the SE/30: a sandboxed Debian shell with full-screen
# programs to try in se30os's terminal. Only the addresses in ALLOW (the
# SE/30's, say), localhost and Docker's networks may connect (TCP
# wrappers); the user is se30, with password PASSWORD.
#   docker build --build-arg PASSWORD=... --build-arg ALLOW="10.1.1.214" .
FROM debian:sid
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    inetutils-inetd inetutils-telnetd tcpd login passwd bash htop vim-nox less tmux procps \
    locales ncurses-term w3m curl ca-certificates git man-db cmatrix \
    && sed -i 's/# en_US.UTF-8/en_US.UTF-8/' /etc/locale.gen && locale-gen \
    && rm -rf /var/lib/apt/lists/*
ARG PASSWORD
ARG ALLOW=""
RUN useradd -m -s /bin/bash se30 && echo "se30:${PASSWORD}" | chpasswd \
    && echo 'telnet stream tcp nowait root /usr/sbin/tcpd /usr/sbin/telnetd' > /etc/inetd.conf \
    && echo "ALL: ${ALLOW} 127.0.0.1 172.16.0.0/255.240.0.0" > /etc/hosts.allow \
    && echo 'ALL: ALL' > /etc/hosts.deny \
    && echo 'LANG=en_US.UTF-8' > /etc/default/locale \
    && printf '\n  Hello, SE/30. Try: htop, vim, tmux, w3m duckduckgo.com, cmatrix\n\n' > /etc/motd \
    && rm -f /etc/profile.d/80-systemd-osc-context.sh /etc/profile.d/70-systemd-shell-extra.sh
CMD ["/usr/sbin/inetutils-inetd", "-d", "/etc/inetd.conf"]

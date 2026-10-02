#!/bin/sh
# The host end of the emulated Ethernet: once MAME has made its TAP
# interface, give it 10.0.2.1, log packets, and run a telnet server whose
# logins get a UTF-8 bash with TERM from the client (xterm-256color).
for i in $(seq 1 600); do
    ip link show tap-mess-0-0 >/dev/null 2>&1 && break
    sleep 1
done
ip addr add 10.0.2.1/24 dev tap-mess-0-0
ip link set tap-mess-0-0 up
tcpdump -l -n -i tap-mess-0-0 > /work/net.txt 2>&1 &
cat > /work/login.sh <<'EOS'
#!/bin/sh
export LANG=en_US.UTF-8 HOME=/root
cd /root
exec /bin/bash -l
EOS
chmod +x /work/login.sh
echo "telnet stream tcp nowait root /usr/sbin/telnetd telnetd -h -E /work/login.sh" \
    > /etc/inetd.conf
/usr/sbin/inetutils-inetd -d /etc/inetd.conf > /work/inetd.log 2>&1 &
wait

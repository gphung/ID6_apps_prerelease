# Setup notes: macOS, split-node head units, and a QNX build VM

Lessons from bringing these apps up on an F87 M2 (NBTevo_Y21432J) from an Apple silicon Mac. Addresses below are examples from that car; read your own.

## ENET connection on macOS

- **Turn off Wi-Fi and disconnect VPNs.** macOS routes 169.254.0.0/16 over whichever interface claimed it first, often Wi-Fi, so pings and SSH silently go to the wrong network. If you must keep Wi-Fi on, bind to the ENET adapter: `ping -b en7 …`, or `IP_BOUND_IF` in scripts.
- The USB adapter re-negotiates often and gets a **new 169.254 address each time**. Read the address at the moment you use it (`ipconfig getifaddr en7`).
- **The gateway's link-local address is per car.** The reference car's 169.254.21.3 is not universal. If the UDP 6811 identification broadcast gets no reply, send the same request unicast to addresses in the ARP table. The gateway answers with `DIAGADR10`, the head unit with `DIAGADR63`. On the M2 the gateway address stayed the same across days and replugs; put it in each app's `gateway-ip.txt`.
- The gateway's internal address (160.48.199.x on the head unit's VLAN) did **not** accept HSFZ on the M2. Use the link-local one.

## Temporary SSH (Bavarian SSH Client)

- If automatic discovery finds nothing, enter the **gateway** address as the car IP. The client then reports the head-unit SSH address.
- A final "malformed relay message: unknown server tag" error did not stop SSH from being enabled.
- The login is `root`. Type the password into a real terminal. A non-interactive shell has no TTY, so SSH sends empty passwords and you only see "Permission denied" three times.
- The head unit runs OpenSSH 5.2 and needs legacy algorithms. A dedicated config keeps them limited to that host, and a control socket lets later commands reuse one login:

```
Host 169.254.199.99
  User root
  UserKnownHostsFile ~/id6-tools/known_hosts
  StrictHostKeyChecking yes
  HostKeyAlgorithms +ssh-rsa
  KexAlgorithms +diffie-hellman-group14-sha1,diffie-hellman-group1-sha1
  Ciphers +aes128-cbc,aes256-cbc,3des-cbc
  MACs +hmac-sha1,hmac-sha1-96
  PubkeyAuthentication no
  ControlMaster auto
  ControlPath ~/id6-tools/ctl/%C
  ControlPersist 20m
```

Run `ssh -F that-config -fN 169.254.199.99` once, type the password, and later commands go through the socket. **Restarting iDrive turns SSH off**, so re-enable it after every restart.

## Split-node head units (hu-jacinto and hu-omap)

- On some units SSH lands on the Jacinto (`uname -n` reports `hu-jacinto`). The HMI, its frameworks and the plugin folder live on the OMAP, which is reachable over Qnet under `/net/hu-omap`.
- The OMAP's `/opt` and `/var` are **absolute symlinks** (`/opt → /fs/sda0/opt`, `/var → /fs/sda1`). Through Qnet they resolve on the Jacinto, so use the real paths:
  - frameworks and HMI: `/net/hu-omap/fs/sda0/opt/hmi/ID5/lib`
  - firmware code: `/net/hu-omap/fs/sda0/opt/sys/etc/nbt_version.txt`
  - plugin folder: `/net/hu-omap/fs/sda1/opt/hmi/data/ID5/Plugins`
- Run commands on the OMAP with `on -f hu-omap …`. Tools there need a library path, for example `on -f hu-omap sh -c "LD_LIBRARY_PATH=/opt/sys/lib:/usr/lib:/lib /opt/sys/bin/LayerManagerControl get screen 0"`.
- A stock unit has **no `Plugins` folder**. The HMI looks for it at the path above, and creating it is all that is needed.

## Missing tools on the head unit

- The Jacinto has `cat ls cp mv mkdir rm on` and little else. There is no `sed`, `head`, `tail`, `du`, `df`, `tar`, `sha256sum` or `md5`.
- The OMAP has `chmod` at `/fs/sda0/bin/chmod`. Run it there: `on -f hu-omap /fs/sda0/bin/chmod 755 /fs/sda1/…/worker`.
- **Upload** with `ssh … "cat > /net/hu-omap/fs/sda1/…/file" < file`. **Verify** by reading the file back and hashing it on the computer.
- In shell loops use `ssh -n`. Otherwise SSH swallows the loop's input and only the first item runs.
- `RHMI-Cache` keeps a `.md5` file next to each cached file, which is handy for checking a backup of the data folder.
- Output passed through `on` and SSH is slow, so **don't time anything with `--trace`**. Over Qnet the trace alone added about 60 ms per line.

## Install procedure that worked

1. Read-only checks: firmware, both framework hashes, screen resolution, gateway, plugin folder contents.
2. Back up `/net/hu-omap/fs/sda1/opt/hmi/data/ID5` to the computer by reading every file, then check sizes and the `.md5` files.
3. Stage each app folder **outside** `ID5` (for example `/net/hu-omap/fs/sda1/id6-staging`), `chmod` the worker on the OMAP, and read back every file.
4. Run the worker by itself on the OMAP with a read-only mode (for example `engine-codes-qnx --host <gateway> --transport hsfz --codes`). This proves the binary loads against the real `libc` and reaches the gateway before the HMI ever sees it.
5. Install one simple app first (Engine Fault Codes), restart iDrive and check it, then add the rest.
6. Rollback: `mv …/ID5/Plugins …/ID5/Plugins.off`, then restart. For an update, move the replaced files to a backup folder outside `Plugins` first.

## QNX SDP 6.5 build VM on Apple silicon

- Use a licensed SDP. The host tools are 32-bit x86, so VirtualBox (ARM guests only) and Rosetta (no 32-bit) don't work. **UTM in Emulate mode** with an i386 guest works.
- **Debian 12 i386.** Debian 13 has no i386 installer. If the network installer can't reach a mirror (corporate VPN or firewall software can block the VM's forwarded traffic), install from **DVD-1** and skip the network mirror.
- UTM quirks:
  - The default emulated NIC was an ISA NE2000, which Debian can't detect. Change it to **Intel e1000**.
  - The i440FX machine has two IDE buses, so a CD, a disk and a second CD fails ("Bus 'ide.2' not found"). Put the QNX ISO on a **USB** drive.
  - The QNX SDP DVD is **bootable**. If it sits in the first CD drive, the VM boots QNX's own installer, which could overwrite the disk. Keep it on USB, after the hard disk in the boot order.
- If the network doesn't come up after install, use systemd-networkd with a static address on UTM's shared network (`Address=192.168.64.10/24`, `Gateway=192.168.64.1`). Then work over SSH from the Mac, which also solves copy and paste and keyboard-layout problems in the console.
- The SDP installer reports "unable to run in graphical mode" over SSH. Run it with **`-console`**. Activation is manual from an offline VM: use `qnxactivate` and the myQNX manual activation page.
- GCC 4.4 with `-Werror` flags things Apple clang doesn't, such as "may be used uninitialized". Build in the VM before installing anything.

## Shell pitfalls on macOS (zsh)

- `$var:r…` is a zsh modifier, so `"$br:refs/…"` silently becomes something else. Write `${br}`.
- A loop variable named `path` overwrites zsh's command search path.
- zsh does not split unquoted variables (`set -- $line`). Run such loops in `bash`.

## Diagnostic findings on the N55 (MEVD17.2.G, SGBD mevd172y)

- The DME answers **two DIDs per `22` request** in the time of one and refuses three or more (NRC 31). Defining a block with `2C 01 F3F0` was also refused.
- A request takes about 30 ms from the computer and about 40 ms from the head unit.
- Engine values, gear-lever encoding and scaling are in [SIGNALS.md](SIGNALS.md#f87-m2-n55-profile).
